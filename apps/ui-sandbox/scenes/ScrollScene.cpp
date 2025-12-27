// Scroll Scene - Demonstrates ScrollContainer and ProgressBar components
// Shows scrollable content with mouse wheel, scrollbar dragging, and auto-layout integration

#include <GL/glew.h>

#include "SceneTypes.h"
#include <cmath>
#include <components/button/Button.h>
#include <components/progress/ProgressBar.h>
#include <components/scroll/ScrollContainer.h>
#include <graphics/Color.h>
#include <input/InputEvent.h>
#include <layout/LayoutContainer.h>
#include <layout/LayoutTypes.h>
#include <memory>
#include <primitives/Primitives.h>
#include <scene/Scene.h>
#include <scene/SceneManager.h>
#include <shapes/Shapes.h>
#include <theme/Theme.h>
#include <utils/Log.h>

namespace {

	constexpr const char* kSceneName = "scroll";

	class ScrollScene : public engine::IScene {
	  public:
		const char* getName() const override { return kSceneName; }
		std::string exportState() override { return "{}"; }

		void onEnter() override {
			using namespace UI;
			using namespace Foundation;

			// Create title
			title = std::make_unique<Text>(Text::Args{
				.position = {50.0F, 30.0F},
				.text = "ScrollContainer & ProgressBar Demo",
				.style = {.color = Color::white(), .fontSize = 20.0F},
				.id = "title"
			});

			// ================================================================
			// Demo 1: Basic ScrollContainer with manual items
			// ================================================================
			scrollLabel1 = std::make_unique<Text>(Text::Args{
				.position = {50.0F, 70.0F},
				.text = "1. ScrollContainer (mouse wheel + drag):",
				.style = {.color = Color::yellow(), .fontSize = 14.0F},
				.id = "scroll_label_1"
			});

			scrollContainer1 = std::make_unique<ScrollContainer>(
				ScrollContainer::Args{.position = {50.0F, 95.0F}, .size = {200.0F, 200.0F}, .id = "scroll_1"}
			);

			// Create a layout container as the scrollable content
			auto content1 = LayoutContainer(
				LayoutContainer::Args{
					.position = {0.0F, 0.0F},
					.size = {192.0F, 0.0F}, // Width excluding scrollbar
					.direction = Direction::Vertical,
					.hAlign = HAlign::Left,
					.id = "content_1"
				}
			);

			// Add items that overflow the viewport
			for (int i = 0; i < 15; ++i) {
				Color itemColor = (i % 2 == 0) ? Color(0.25F, 0.35F, 0.45F, 1.0F) : Color(0.30F, 0.40F, 0.50F, 1.0F);

				content1.addChild(Rectangle(
					Rectangle::Args{
						.size = {180.0F, 30.0F},
						.style = {.fill = itemColor, .border = BorderStyle{.color = Color(0.4F, 0.5F, 0.6F, 1.0F), .width = 1.0F}},
						.margin = 2.0F,
						.id = nullptr
					}
				));
			}

			scrollContainer1->addChild(std::move(content1));

			// ================================================================
			// Demo 2: ScrollContainer with LayoutContainer + Buttons
			// ================================================================
			scrollLabel2 = std::make_unique<Text>(Text::Args{
				.position = {300.0F, 70.0F},
				.text = "2. Scrollable Button List:",
				.style = {.color = Color::yellow(), .fontSize = 14.0F},
				.id = "scroll_label_2"
			});

			scrollContainer2 = std::make_unique<ScrollContainer>(
				ScrollContainer::Args{.position = {300.0F, 95.0F}, .size = {220.0F, 200.0F}, .id = "scroll_2"}
			);

			auto content2 = LayoutContainer(
				LayoutContainer::Args{
					.position = {0.0F, 0.0F},
					.size = {212.0F, 0.0F},
					.direction = Direction::Vertical,
					.hAlign = HAlign::Center,
					.id = "content_2"
				}
			);

			for (int i = 0; i < 12; ++i) {
				content2.addChild(Button(
					Button::Args{
						.label = "Button " + std::to_string(i + 1),
						.size = {180.0F, 35.0F},
						.type = (i % 3 == 0) ? Button::Type::Secondary : Button::Type::Primary,
						.margin = 3.0F,
						.onClick = [i]() { LOG_INFO(UI, "Button %d clicked!", i + 1); },
						.id = nullptr
					}
				));
			}

			scrollContainer2->addChild(std::move(content2));

			// ================================================================
			// Demo 3: ProgressBar showcase
			// ================================================================
			progressLabel = std::make_unique<Text>(Text::Args{
				.position = {550.0F, 70.0F},
				.text = "3. ProgressBar Examples:",
				.style = {.color = Color::yellow(), .fontSize = 14.0F},
				.id = "progress_label"
			});

			// Progress bars with different values and colors
			progressBar1 = std::make_unique<ProgressBar>(ProgressBar::Args{
				.position = {550.0F, 100.0F},
				.size = {200.0F, 16.0F},
				.value = 0.75F,
				.fillColor = Theme::Colors::statusActive, // Green
				.id = "progress_75"
			});

			progressBar2 = std::make_unique<ProgressBar>(ProgressBar::Args{
				.position = {550.0F, 130.0F},
				.size = {200.0F, 16.0F},
				.value = 0.45F,
				.fillColor = Theme::Colors::statusPending, // Yellow
				.id = "progress_45"
			});

			progressBar3 = std::make_unique<ProgressBar>(ProgressBar::Args{
				.position = {550.0F, 160.0F},
				.size = {200.0F, 16.0F},
				.value = 0.25F,
				.fillColor = Theme::Colors::statusBlocked, // Red
				.id = "progress_25"
			});

			// Progress bar with label
			progressBarLabel = std::make_unique<Text>(Text::Args{
				.position = {550.0F, 200.0F},
				.text = "With Label:",
				.style = {.color = Color(0.7F, 0.7F, 0.75F, 1.0F), .fontSize = 12.0F},
				.id = "progress_bar_label"
			});

			progressBar4 = std::make_unique<ProgressBar>(ProgressBar::Args{
				.position = {550.0F, 220.0F},
				.size = {200.0F, 14.0F},
				.value = 0.6F,
				.fillColor = Color(0.3F, 0.6F, 0.9F, 1.0F), // Blue
				.label = "Health",
				.labelWidth = 50.0F,
				.labelGap = 5.0F,
				.id = "progress_health"
			});

			progressBar5 = std::make_unique<ProgressBar>(ProgressBar::Args{
				.position = {550.0F, 245.0F},
				.size = {200.0F, 14.0F},
				.value = 0.85F,
				.fillColor = Color(0.9F, 0.6F, 0.2F, 1.0F), // Orange
				.label = "Mana",
				.labelWidth = 50.0F,
				.labelGap = 5.0F,
				.id = "progress_mana"
			});

			// ================================================================
			// Instructions
			// ================================================================
			instructions = std::make_unique<Text>(Text::Args{
				.position = {50.0F, 320.0F},
				.text = "Mouse wheel to scroll | Click track to jump | Drag thumb to scroll",
				.style = {.color = Color(0.6F, 0.6F, 0.7F, 1.0F), .fontSize = 12.0F},
				.id = "instructions"
			});

			LOG_INFO(UI, "Scroll scene initialized");
		}

		void onExit() override {
			title.reset();
			scrollLabel1.reset();
			scrollLabel2.reset();
			progressLabel.reset();
			progressBarLabel.reset();
			instructions.reset();
			scrollContainer1.reset();
			scrollContainer2.reset();
			progressBar1.reset();
			progressBar2.reset();
			progressBar3.reset();
			progressBar4.reset();
			progressBar5.reset();
			LOG_INFO(UI, "Scroll scene exited");
		}

		bool handleInput(UI::InputEvent& event) override {
			// Dispatch to scroll containers
			if (scrollContainer1 && scrollContainer1->handleEvent(event)) {
				return true;
			}
			if (scrollContainer2 && scrollContainer2->handleEvent(event)) {
				return true;
			}
			return false;
		}

		void update(float deltaTime) override {
			if (scrollContainer1) {
				scrollContainer1->update(deltaTime);
			}
			if (scrollContainer2) {
				scrollContainer2->update(deltaTime);
			}

			// Animate progress bars for demo
			animTime += deltaTime;
			if (progressBar1) {
				progressBar1->setValue(0.5F + 0.5F * std::sin(animTime * 0.5F));
			}
		}

		void render() override {
			// Clear background
			glClearColor(0.10F, 0.10F, 0.13F, 1.0F);
			glClear(GL_COLOR_BUFFER_BIT);

			// Render labels
			if (title) {
				title->render();
			}
			if (scrollLabel1) {
				scrollLabel1->render();
			}
			if (scrollLabel2) {
				scrollLabel2->render();
			}
			if (progressLabel) {
				progressLabel->render();
			}
			if (progressBarLabel) {
				progressBarLabel->render();
			}
			if (instructions) {
				instructions->render();
			}

			// Render scroll containers
			if (scrollContainer1) {
				scrollContainer1->render();
			}
			if (scrollContainer2) {
				scrollContainer2->render();
			}

			// Render progress bars
			if (progressBar1) {
				progressBar1->render();
			}
			if (progressBar2) {
				progressBar2->render();
			}
			if (progressBar3) {
				progressBar3->render();
			}
			if (progressBar4) {
				progressBar4->render();
			}
			if (progressBar5) {
				progressBar5->render();
			}
		}

	  private:
		// Labels
		std::unique_ptr<UI::Text> title;
		std::unique_ptr<UI::Text> scrollLabel1;
		std::unique_ptr<UI::Text> scrollLabel2;
		std::unique_ptr<UI::Text> progressLabel;
		std::unique_ptr<UI::Text> progressBarLabel;
		std::unique_ptr<UI::Text> instructions;

		// Scroll containers
		std::unique_ptr<UI::ScrollContainer> scrollContainer1;
		std::unique_ptr<UI::ScrollContainer> scrollContainer2;

		// Progress bars
		std::unique_ptr<UI::ProgressBar> progressBar1;
		std::unique_ptr<UI::ProgressBar> progressBar2;
		std::unique_ptr<UI::ProgressBar> progressBar3;
		std::unique_ptr<UI::ProgressBar> progressBar4;
		std::unique_ptr<UI::ProgressBar> progressBar5;

		// Animation
		float animTime = 0.0F;
	};

} // anonymous namespace

// Export scene info for registry
namespace ui_sandbox::scenes {
	extern const ui_sandbox::SceneInfo Scroll = {kSceneName, []() { return std::make_unique<ScrollScene>(); }};
} // namespace ui_sandbox::scenes
