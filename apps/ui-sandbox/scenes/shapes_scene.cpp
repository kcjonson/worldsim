// Shapes Scene - 2D Rendering Primitives Showcase
// Demonstrates all 2D rendering primitives (rectangles, lines, borders)

#include <graphics/color.h>
#include <graphics/rect.h>
#include <primitives/primitives.h>
#include <scene/scene.h>
#include <scene/scene_manager.h>

#include <GL/glew.h>

namespace {

	class ShapesScene : public engine::IScene {
	  public:
		void OnEnter() override {
			// No initialization needed for shapes scene
		}

		void HandleInput(float dt) override {
			// No input handling needed - static scene
		}

		void Update(float dt) override {
			// No update logic needed - static shapes
		}

		void Render() override {
			using namespace Foundation;

			// Clear background
			glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT);

			// Draw filled rectangles with IDs for inspection
			Renderer::Primitives::DrawRect({.bounds = {50, 50, 200, 100}, .style = {.fill = Color::Red()}, .id = "red_rect"});

			Renderer::Primitives::DrawRect({.bounds = {300, 50, 200, 100}, .style = {.fill = Color::Green()}, .id = "green_rect"});

			Renderer::Primitives::DrawRect({.bounds = {550, 50, 200, 100}, .style = {.fill = Color::Blue()}, .id = "blue_rect"});

			// Draw rectangles with borders (no fill)
			Renderer::Primitives::DrawRect(
				{.bounds = {50, 200, 200, 100},
				 .style = {.fill = Color::Transparent(), .border = BorderStyle{.color = Color::Yellow(), .width = 3.0f}},
				 .id = "yellow_border"}
			);

			Renderer::Primitives::DrawRect(
				{.bounds = {300, 200, 200, 100},
				 .style = {.fill = Color::Transparent(), .border = BorderStyle{.color = Color::Cyan(), .width = 3.0f}},
				 .id = "cyan_border"}
			);

			// Rectangle with both fill and border
			Renderer::Primitives::DrawRect(
				{.bounds = {550, 200, 200, 100},
				 .style =
					 {.fill = Color(0.5f, 0.0f, 0.5f, 1.0f), // Purple
					  .border = BorderStyle{.color = Color::White(), .width = 2.0f}},
				 .id = "purple_rect_bordered"}
			);

			// Draw a grid of small rectangles (batching test)
			for (int y = 0; y < 10; y++) {
				for (int x = 0; x < 10; x++) {
					float hue = (x * 10 + y) / 100.0f;
					Color color(hue, 1.0f - hue, 0.5f, 1.0f);

					Renderer::Primitives::DrawRect(
						{.bounds = {50.0f + x * 25.0f, 350.0f + y * 20.0f, 20.0f, 15.0f}, .style = {.fill = color}}
					);
				}
			}
		}

		void OnExit() override {
			// No cleanup needed for shapes scene
		}

		std::string ExportState() override {
			// Export basic scene info (static scene, no dynamic state)
			return R"({
			"scene": "shapes",
			"description": "2D primitives showcase",
			"rectangles": 9,
			"grid_size": "10x10"
		})";
		}

		const char* GetName() const override { return "shapes"; }
	};

	// Register scene with SceneManager
	static bool s_registered = []() {
		engine::SceneManager::Get().RegisterScene("shapes", []() { return std::make_unique<ShapesScene>(); });
		return true;
	}();

} // anonymous namespace
