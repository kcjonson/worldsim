// Layer Scene - UI Component Hierarchy Showcase
// Demonstrates Component hierarchy with AddChild and automatic z-ordering

#include <component/component.h>
#include <component/container.h>
#include <core/render_context.h>
#include <graphics/color.h>
#include <primitives/primitives.h>
#include <scene/scene.h>
#include <scene/scene_manager.h>
#include <shapes/shapes.h>
#include <utils/log.h>

#include <GL/glew.h>

namespace {

	class LayerScene : public engine::IScene {
	  public:
		void OnEnter() override {
			using namespace UI;
			using namespace Foundation;

			// Root container owns all UI elements
			m_root = std::make_unique<Container>();

			// Background rectangle (z=0, default)
			m_root->AddChild(Rectangle(
				Rectangle::Args{
					.position = {100.0F, 100.0F},
					.size = {600.0F, 400.0F},
					.style = {.fill = Color(0.15F, 0.15F, 0.2F, 1.0F)},
					.id = "background"
				}
			));

			// Green panel with title (z=1)
			m_root->AddChild(Rectangle(
				Rectangle::Args{.position = {150.0F, 120.0F}, .size = {350.0F, 200.0F}, .style = {.fill = Color::Green()}, .zIndex = 1}
			));

			// Title text on green panel (z=2)
			m_root->AddChild(Text(
				Text::Args{
					.position = {200.0F, 200.0F},
					.text = "Component Hierarchy Demo",
					.style = {.color = Color::Yellow(), .fontSize = 24.0F, .hAlign = HorizontalAlign::Left, .vAlign = VerticalAlign::Top},
					.zIndex = 2
				}
			));

			// Sidebar container (z=1)
			m_root->AddChild(Rectangle(
				Rectangle::Args{
					.position = {550.0F, 150.0F},
					.size = {150.0F, 350.0F},
					.style = {.fill = Color(0.3F, 0.3F, 0.35F, 1.0F), .border = BorderStyle{.color = Color::Cyan(), .width = 2.0F}},
					.zIndex = 1
				}
			));

			// Sidebar buttons (z=2 for backgrounds, z=3 for text)
			for (int i = 0; i < 5; i++) {
				auto const iFloat = static_cast<float>(i);

				// Button background
				m_root->AddChild(Rectangle(
					Rectangle::Args{
						.position = {560.0F, 170.0F + (iFloat * 60.0F)},
						.size = {130.0F, 50.0F},
						.style = {.fill = Color(0.4F, 0.4F, 0.45F, 1.0F), .border = BorderStyle{.color = Color::White(), .width = 1.0F}},
						.zIndex = 2
					}
				));

				// Button label
				m_root->AddChild(Text(
					Text::Args{
						.position = {625.0F, 195.0F + (iFloat * 60.0F)},
						.text = "Button " + std::to_string(i + 1),
						.style = {.color = Color::White(), .fontSize = 14.0F, .hAlign = HorizontalAlign::Center},
						.zIndex = 3
					}
				));
			}

			// Overlapping rectangles - ascending zIndex (back to front)
			// These demonstrate z-ordering: later rectangles overlap earlier ones
			for (int i = 0; i < 4; i++) {
				float offset = static_cast<float>(i) * 30.0F;
				float hue = static_cast<float>(i) / 4.0F;
				Color color(hue, 1.0F - hue, 0.5F, 0.8F);

				m_root->AddChild(Rectangle(
					Rectangle::Args{
						.position = {150.0F + offset, 350.0F + offset},
						.size = {100.0F, 100.0F},
						.style = {.fill = color, .border = BorderStyle{.color = Color::White(), .width = 2.0F}},
						.zIndex = static_cast<short>(i)
					}
				));
			}

			// Another set - descending zIndex (demonstrating z-ordering overrides creation order)
			// First created rectangle has highest z, so it renders on TOP
			for (int i = 0; i < 4; i++) {
				float offset = static_cast<float>(i) * 30.0F;
				float hue = static_cast<float>(i) / 4.0F;
				Color color(hue, 0.5F, 1.0F - hue, 0.8F);

				m_root->AddChild(Rectangle(
					Rectangle::Args{
						.position = {450.0F + offset, 350.0F + offset},
						.size = {100.0F, 100.0F},
						.style = {.fill = color, .border = BorderStyle{.color = Color::White(), .width = 2.0F}},
						.zIndex = static_cast<short>(3 - i) // Reverse z-order
					}
				));
			}

			LOG_INFO(UI, "Layer scene initialized with Component hierarchy");
		}

		void HandleInput(float /*dt*/) override {
			if (m_root) {
				m_root->HandleInput();
			}
		}

		void Update(float /*dt*/) override {
			if (m_root) {
				m_root->Update(0.0F);
			}
		}

		void Render() override {
			glClearColor(0.1F, 0.1F, 0.15F, 1.0F);
			glClear(GL_COLOR_BUFFER_BIT);

			if (m_root) {
				m_root->Render();
			}
		}

		void OnExit() override { m_root.reset(); }

		std::string ExportState() override { return "{\"scene\": \"layer\", \"description\": \"Component hierarchy demo\"}"; }

		const char* GetName() const override { return "layer"; }

	  private:
		std::unique_ptr<UI::Container> m_root;
	};

	// Register scene with SceneManager
	bool g_registered = []() {
		engine::SceneManager::Get().RegisterScene("layer", []() { return std::make_unique<LayerScene>(); });
		return true;
	}();

} // anonymous namespace
