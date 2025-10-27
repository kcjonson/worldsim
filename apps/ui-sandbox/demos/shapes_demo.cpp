// Shapes Demo Implementation
// Demonstrates all 2D rendering primitives (rectangles, lines, borders)

#include "demo.h"
#include "primitives/primitives.h"
#include "graphics/color.h"
#include "graphics/rect.h"

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

	// Draw test rectangles
	Renderer::Primitives::DrawRect(Rect(50, 50, 200, 100), Color::Red());
	Renderer::Primitives::DrawRect(Rect(300, 50, 200, 100), Color::Green());
	Renderer::Primitives::DrawRect(Rect(550, 50, 200, 100), Color::Blue());

	// Draw some bordered rectangles
	Renderer::Primitives::DrawRectBorder(Rect(50, 200, 200, 100), Color::Yellow(), 3.0f);
	Renderer::Primitives::DrawRectBorder(Rect(300, 200, 200, 100), Color::Cyan(), 3.0f);

	// Draw a grid of small rectangles (batching test)
	for (int y = 0; y < 10; y++) {
		for (int x = 0; x < 10; x++) {
			float hue = (x * 10 + y) / 100.0f;
			Color color(hue, 1.0f - hue, 0.5f, 1.0f);
			Renderer::Primitives::DrawRect(Rect(50 + x * 25, 350 + y * 20, 20, 15), color);
		}
	}

	// End primitive rendering (flushes batches)
	Renderer::Primitives::EndFrame();
}

void Shutdown() {
	// No cleanup needed for shapes demo
}

} // namespace Demo
