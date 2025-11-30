// Clip Scene - Clipping and Scrolling Demo
// Demonstrates clipping system features: rect clipping, nested clips, scrolling
// See /docs/technical/ui-framework/clipping.md for design documentation.

#include <graphics/clip_types.h>
#include <graphics/color.h>
#include <primitives/primitives.h>
#include <scene/scene.h>
#include <scene/scene_manager.h>
#include <shapes/shapes.h>
#include <utils/log.h>

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <cmath>
#include <memory>
#include <vector>

namespace {

	class ClipScene : public engine::IScene {
	  public:
		void onEnter() override {
			LOG_INFO(UI, "Clip Scene - Clipping and Scrolling Demo");
			LOG_INFO(UI, "Press 'C' to toggle clipping on/off");
		}

		void handleInput(float /*dt*/) override {
			// Toggle clipping with 'C' key
			static bool lastKeyState = false;
			bool		currentKeyState = glfwGetKey(glfwGetCurrentContext(), GLFW_KEY_C) == GLFW_PRESS;

			if (currentKeyState && !lastKeyState) {
				clippingEnabled = !clippingEnabled;
				LOG_INFO(UI, "Clipping %s", clippingEnabled ? "ENABLED" : "DISABLED");
			}
			lastKeyState = currentKeyState;
		}

		void update(float dt) override {
			// Animate scroll position for demo
			scrollY += dt * 30.0F;
			if (scrollY > 150.0F) {
				scrollY = 0.0F;
			}
		}

		void render() override {
			using namespace Foundation;
			using namespace Renderer;

			glClearColor(0.1F, 0.1F, 0.15F, 1.0F);
			glClear(GL_COLOR_BUFFER_BIT);

			// Helper to render text (creates temporary Text shape)
			auto drawText = [](const std::string& str, float x, float y, float fontSize, const Color& color) {
				UI::Text text(UI::Text::Args{.position = {x, y}, .text = str, .style = {.color = color, .fontSize = fontSize}});
				text.render();
			};

			// Title
			Primitives::drawRect({.bounds = {20, 20, 400, 40}, .style = {.fill = Color(0.0F, 0.0F, 0.0F, 0.5F)}});
			drawText("Clip Scene - Clipping Demo", 30, 30, 20.0F, Color::white());

			// Status indicator
			Color statusColor = clippingEnabled ? Color::green() : Color::red();
			Primitives::drawRect({.bounds = {20, 70, 300, 30}, .style = {.fill = Color(0.0F, 0.0F, 0.0F, 0.5F)}});
			drawText(clippingEnabled ? "Clipping: ON (press C)" : "Clipping: OFF (press C)", 30, 78, 14.0F, statusColor);

			// ========================================================================
			// Section 1: Basic Rect Clipping Demo
			// ========================================================================

			// Section label background
			Primitives::drawRect({.bounds = {50, 120, 300, 25}, .style = {.fill = Color(0.2F, 0.2F, 0.3F, 1.0F)}});
			drawText("1. Basic Rect Clipping + Text", 55, 125, 14.0F, Color::white());

			// Clip boundary indicator (always visible)
			Primitives::drawRect(
				{.bounds = {50, 160, 300, 80},
				 .style = {.fill = Color(0.15F, 0.15F, 0.2F, 1.0F), .border = BorderStyle{.color = Color::cyan(), .width = 2.0F}}}
			);

			// Apply clipping for the content
			if (clippingEnabled) {
				ClipSettings clipSettings;
				clipSettings.shape = ClipRect{.bounds = Rect{50.0F, 160.0F, 300.0F, 80.0F}};
				clipSettings.mode = ClipMode::Inside;
				Primitives::pushClip(clipSettings);
			}

			// Content that overflows the clip boundary - rectangles with text
			// These extend beyond the 300x80 clip region
			Primitives::drawRect({.bounds = {60, 170, 350, 25}, .style = {.fill = Color(0.8F, 0.3F, 0.3F, 1.0F)}}); // Red - overflows right
			drawText("This text extends past the clip boundary ->>>>>", 65, 175, 12.0F, Color::white());

			Primitives::drawRect({.bounds = {60, 200, 280, 25}, .style = {.fill = Color(0.3F, 0.8F, 0.3F, 1.0F)}}); // Green - fits
			drawText("This text fits inside", 65, 205, 12.0F, Color::white());

			Primitives::drawRect(
				{.bounds = {60, 230, 320, 25}, .style = {.fill = Color(0.3F, 0.3F, 0.8F, 1.0F)}}
			); // Blue - overflows bottom+right
			drawText("This text clips at bottom edge", 65, 235, 12.0F, Color::white());

			if (clippingEnabled) {
				Primitives::popClip();
			}

			// ========================================================================
			// Section 2: Scrollable List Demo (simulated with animated offset)
			// ========================================================================

			// Section label
			Primitives::drawRect({.bounds = {50, 270, 300, 25}, .style = {.fill = Color(0.2F, 0.2F, 0.3F, 1.0F)}});
			drawText("2. Scrollable List with Text", 55, 275, 14.0F, Color::white());

			// Scroll container boundary
			Primitives::drawRect(
				{.bounds = {50, 310, 300, 120},
				 .style = {.fill = Color(0.12F, 0.12F, 0.18F, 1.0F), .border = BorderStyle{.color = Color::green(), .width = 2.0F}}}
			);

			// Apply clipping for scroll container
			if (clippingEnabled) {
				ClipSettings clipSettings;
				clipSettings.shape = ClipRect{.bounds = Rect{50.0F, 310.0F, 300.0F, 120.0F}};
				Primitives::pushClip(clipSettings);
			}

			// Draw list items with text (more than can fit, offset by scroll position)
			const char* listItems[] = {
				"Item 1 - First Entry",
				"Item 2 - Second Entry",
				"Item 3 - Third Entry",
				"Item 4 - Fourth Entry",
				"Item 5 - Fifth Entry",
				"Item 6 - Sixth Entry",
				"Item 7 - Seventh Entry",
				"Item 8 - Eighth Entry"
			};
			for (int i = 0; i < 8; i++) {
				float baseY = 320.0F + (static_cast<float>(i) * 35.0F) - scrollY;
				Color itemColor = (i % 2 == 0) ? Color(0.25F, 0.25F, 0.3F, 1.0F) : Color(0.3F, 0.3F, 0.35F, 1.0F);

				Primitives::drawRect({.bounds = {60, baseY, 280, 30}, .style = {.fill = itemColor}});
				drawText(listItems[i], 70, baseY + 6, 14.0F, Color::white());
			}

			if (clippingEnabled) {
				Primitives::popClip();
			}

			// ========================================================================
			// Section 3: Nested Clips Demo
			// ========================================================================

			// Section label
			Primitives::drawRect({.bounds = {400, 120, 250, 25}, .style = {.fill = Color(0.2F, 0.2F, 0.3F, 1.0F)}});
			drawText("3. Nested Clips", 405, 125, 14.0F, Color::white());

			// Outer clip boundary (red)
			Primitives::drawRect(
				{.bounds = {400, 160, 250, 180},
				 .style = {.fill = Color(0.2F, 0.1F, 0.1F, 1.0F), .border = BorderStyle{.color = Color::red(), .width = 2.0F}}}
			);

			// Apply outer clip
			if (clippingEnabled) {
				ClipSettings outerClip;
				outerClip.shape = ClipRect{.bounds = Rect{400.0F, 160.0F, 250.0F, 180.0F}};
				Primitives::pushClip(outerClip);
			}

			// Inner clip boundary (green) - inside outer
			Primitives::drawRect(
				{.bounds = {430, 190, 190, 120},
				 .style = {.fill = Color(0.1F, 0.2F, 0.1F, 1.0F), .border = BorderStyle{.color = Color::green(), .width = 2.0F}}}
			);

			// Apply inner clip (nested - should intersect with outer)
			if (clippingEnabled) {
				ClipSettings innerClip;
				innerClip.shape = ClipRect{.bounds = Rect{430.0F, 190.0F, 190.0F, 120.0F}};
				Primitives::pushClip(innerClip);
			}

			// Content that crosses both boundaries (purple rectangle)
			// This should only be visible within the intersection of both clips
			Primitives::drawRect({.bounds = {410, 220, 220, 100}, .style = {.fill = Color(0.6F, 0.4F, 0.8F, 0.9F)}});

			// Pop both clips
			if (clippingEnabled) {
				Primitives::popClip(); // Inner
				Primitives::popClip(); // Outer
			}

			// ========================================================================
			// Section 4: Future Features (Placeholders)
			// ========================================================================

			Primitives::drawRect({.bounds = {400, 370, 300, 25}, .style = {.fill = Color(0.15F, 0.15F, 0.2F, 1.0F)}});
			drawText("4. Future: Rounded/Circle Clips", 405, 375, 14.0F, Color(0.5F, 0.5F, 0.5F, 1.0F));

			// Placeholder items (grayed out)
			Primitives::drawRect({.bounds = {410, 410, 200, 20}, .style = {.fill = Color(0.2F, 0.2F, 0.2F, 0.5F)}});
			Primitives::drawRect({.bounds = {410, 435, 180, 20}, .style = {.fill = Color(0.2F, 0.2F, 0.2F, 0.5F)}});
			Primitives::drawRect({.bounds = {410, 460, 220, 20}, .style = {.fill = Color(0.2F, 0.2F, 0.2F, 0.5F)}});

			// ========================================================================
			// Instructions
			// ========================================================================
			Primitives::drawRect({.bounds = {50, 470, 400, 30}, .style = {.fill = Color(0.0F, 0.0F, 0.3F, 0.5F)}});
			drawText("Press 'C' to toggle clipping | Watch scrolling list animate", 60, 478, 12.0F, Color::white());
		}

		void onExit() override { LOG_INFO(UI, "Exiting Clip Scene"); }

		std::string exportState() override {
			char buf[128];
			snprintf(buf, sizeof(buf), R"({"clipping": %s, "scrollY": %.1F})", clippingEnabled ? "true" : "false", scrollY);
			return {buf};
		}

		const char* getName() const override { return "clip"; }

	  private:
		bool  clippingEnabled = true; // Start with clipping enabled to show it working
		float scrollY = 0.0F;
	};

	// Register scene with SceneManager
	bool g_registered = []() {
		engine::SceneManager::Get().registerScene("clip", []() { return std::make_unique<ClipScene>(); });
		return true;
	}();

} // anonymous namespace
