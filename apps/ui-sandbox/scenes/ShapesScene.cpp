// Shapes Scene - 2D Rendering Primitives Showcase
// Demonstrates all 2D rendering primitives (rectangles, lines, borders)

#include <graphics/Color.h>
#include <graphics/Rect.h>
#include <primitives/Primitives.h>
#include <scene/Scene.h>
#include <scene/SceneManager.h>

#include <GL/glew.h>

namespace {

constexpr const char* kSceneName = "shapes";

class ShapesScene : public engine::IScene {
	  public:
		void onEnter() override {
			// No initialization needed for shapes scene
		}

		void handleInput(float dt) override {
			// No input handling needed - static scene
		}

		void update(float dt) override {
			// No update logic needed - static shapes
		}

		void render() override {
			using namespace Foundation;

			// Clear background
			glClearColor(0.1F, 0.1F, 0.15F, 1.0F);
			glClear(GL_COLOR_BUFFER_BIT);

			// Draw filled rectangles with IDs for inspection
			Renderer::Primitives::drawRect({.bounds = {50, 50, 200, 100}, .style = {.fill = Color::red()}, .id = "red_rect"});

			Renderer::Primitives::drawRect({.bounds = {300, 50, 200, 100}, .style = {.fill = Color::green()}, .id = "green_rect"});

			Renderer::Primitives::drawRect({.bounds = {550, 50, 200, 100}, .style = {.fill = Color::blue()}, .id = "blue_rect"});

			// Draw rectangles with borders (no fill)
			Renderer::Primitives::drawRect(
				{.bounds = {50, 200, 200, 100},
				 .style = {.fill = Color::transparent(), .border = BorderStyle{.color = Color::yellow(), .width = 3.0F}},
				 .id = "yellow_border"}
			);

			Renderer::Primitives::drawRect(
				{.bounds = {300, 200, 200, 100},
				 .style = {.fill = Color::transparent(), .border = BorderStyle{.color = Color::cyan(), .width = 3.0F}},
				 .id = "cyan_border"}
			);

			// Rectangle with both fill and border
			Renderer::Primitives::drawRect(
				{.bounds = {550, 200, 200, 100},
				 .style =
					 {.fill = Color(0.5F, 0.0F, 0.5F, 1.0F), // Purple
					  .border = BorderStyle{.color = Color::white(), .width = 2.0F}},
				 .id = "purple_rect_bordered"}
			);

			// Draw a grid of small rectangles (batching test)
			for (int y = 0; y < 10; y++) {
				for (int x = 0; x < 10; x++) {
					float hue = static_cast<float>((x * 10) + y) / 100.0F;
					Color color(hue, 1.0F - hue, 0.5F, 1.0F);

					Renderer::Primitives::drawRect(
						{.bounds = {50.0F + (static_cast<float>(x) * 25.0F), 350.0F + (static_cast<float>(y) * 20.0F), 20.0F, 15.0F},
						 .style = {.fill = color}}
					);
				}
			}
		}

		void onExit() override {
			// No cleanup needed for shapes scene
		}

		std::string exportState() override { // NOLINT(readability-convert-member-functions-to-static)
			// Export basic scene info (static scene, no dynamic state)
			return R"({
			"scene": "shapes",
			"description": "2D primitives showcase",
			"rectangles": 9,
			"grid_size": "10x10"
		})";
		}

		const char* getName() const override { return kSceneName; }
};

} // anonymous namespace

// Export factory and name for scene registry
namespace ui_sandbox::scenes {
	std::unique_ptr<engine::IScene> createShapesScene() { return std::make_unique<ShapesScene>(); }
	const char* getShapesSceneName() { return kSceneName; }
}
