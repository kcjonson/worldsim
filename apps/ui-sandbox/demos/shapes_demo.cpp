// Shapes Demo Implementation
// Demonstrates all 2D rendering primitives (rectangles, lines, borders)

#include "demo.h"
#include "graphics/color.h"
#include "graphics/rect.h"
#include "primitives/primitives.h"

#include <GL/glew.h>

namespace Demo {

	void Init() {
		// No initialization needed for shapes demo
	}

	void Render() {
		using namespace Foundation;

		// Clear background
		glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);

		// Begin primitive rendering
		Renderer::Primitives::BeginFrame();

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

				Renderer::Primitives::DrawRect({.bounds = {50.0f + x * 25.0f, 350.0f + y * 20.0f, 20.0f, 15.0f}, .style = {.fill = color}});
			}
		}

		// End primitive rendering (flushes batches)
		Renderer::Primitives::EndFrame();
	}

	void Shutdown() {
		// No cleanup needed for shapes demo
	}

} // namespace Demo
