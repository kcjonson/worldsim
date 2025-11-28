// Layer Scene - UI Component Hierarchy Showcase
// Demonstrates Component hierarchy with shapes and containers

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
#include <algorithm>
#include <memory>
#include <vector>

namespace {

	class LayerScene : public engine::IScene {
	  public:
		void OnEnter() override {
			using namespace UI;
			using namespace Foundation;

			// Background rectangle
			m_shapes.push_back(std::make_unique<Rectangle>(Rectangle::Args{
				.position = {100.0F, 100.0F},
				.size = {600.0F, 400.0F},
				.style = {.fill = Color(0.15F, 0.15F, 0.2F, 1.0F)},
				.id = "background"
			}));

			// Red rectangle with border
			m_shapes.push_back(std::make_unique<Rectangle>(Rectangle::Args{
				.position = {150.0F, 150.0F},
				.size = {200.0F, 150.0F},
				.style = {.fill = Color::Red(), .border = BorderStyle{.color = Color::White(), .width = 3.0F}},
				.id = "red_rect"
			}));

			// Blue circle with border
			m_shapes.push_back(std::make_unique<Circle>(Circle::Args{
				.center = {400.0F, 250.0F},
				.radius = 80.0F,
				.style = {.fill = Color::Blue(), .border = BorderStyle{.color = Color::Cyan(), .width = 3.0F}},
				.id = "blue_circle"
			}));

			// Diagonal line
			m_shapes.push_back(std::make_unique<Line>(Line::Args{
				.start = {150.0F, 150.0F},
				.end = {500.0F, 400.0F},
				.style = {.color = Color::Green(), .width = 4.0F},
				.id = "diagonal_line"
			}));

			// Title text
			m_shapes.push_back(std::make_unique<Text>(Text::Args{
				.position = {200.0F, 180.0F},
				.text = "Component Hierarchy Demo",
				.style = {.color = Color::Yellow(), .fontSize = 24.0F, .hAlign = HorizontalAlign::Left, .vAlign = VerticalAlign::Top},
				.id = "title_text"
			}));

			// Sidebar
			m_shapes.push_back(std::make_unique<Rectangle>(Rectangle::Args{
				.position = {550.0F, 150.0F},
				.size = {150.0F, 350.0F},
				.style = {.fill = Color(0.3F, 0.3F, 0.35F, 1.0F), .border = BorderStyle{.color = Color::Cyan(), .width = 2.0F}},
				.id = "sidebar"
			}));

			// Sidebar items (fake buttons)
			for (int i = 0; i < 5; i++) {
				auto const iFloat = static_cast<float>(i);

				// Button background
				m_shapes.push_back(std::make_unique<Rectangle>(Rectangle::Args{
					.position = {560.0F, 170.0F + (iFloat * 60.0F)},
					.size = {130.0F, 50.0F},
					.style = {.fill = Color(0.4F, 0.4F, 0.45F, 1.0F), .border = BorderStyle{.color = Color::White(), .width = 1.0F}}
				}));

				// Button label
				m_shapes.push_back(std::make_unique<Text>(Text::Args{
					.position = {625.0F, 195.0F + (iFloat * 60.0F)},
					.text = "Button " + std::to_string(i + 1),
					.style = {.color = Color::White(), .fontSize = 14.0F, .hAlign = HorizontalAlign::Center}
				}));
			}

			// Overlapping rectangles - ascending zIndex (back to front)
			for (int i = 0; i < 4; i++) {
				float offset = static_cast<float>(i) * 30.0F;
				float hue = static_cast<float>(i) / 4.0F;
				Color color(hue, 1.0F - hue, 0.5F, 0.8F);

				m_shapes.push_back(std::make_unique<Rectangle>(Rectangle::Args{
					.position = {150.0F + offset, 350.0F + offset},
					.size = {100.0F, 100.0F},
					.style = {.fill = color, .border = BorderStyle{.color = Color::White(), .width = 2.0F}},
					.zIndex = static_cast<short>(i)
				}));
			}

			// Another set - descending zIndex (front to back, demonstrating z-ordering)
			for (int i = 0; i < 4; i++) {
				float offset = static_cast<float>(i) * 30.0F;
				float hue = static_cast<float>(i) / 4.0F;
				Color color(hue, 0.5F, 1.0F - hue, 0.8F);

				m_shapes.push_back(std::make_unique<Rectangle>(Rectangle::Args{
					.position = {450.0F + offset, 350.0F + offset},
					.size = {100.0F, 100.0F},
					.style = {.fill = color, .border = BorderStyle{.color = Color::White(), .width = 2.0F}},
					.zIndex = static_cast<short>(3 - i)  // Reverse z-order
				}));
			}

			LOG_INFO(UI, "Layer scene initialized with {} shapes", m_shapes.size());
		}

		void HandleInput(float /*dt*/) override {
			// No input handling needed - static scene
		}

		void Update(float /*dt*/) override {
			// Static scene - no updates needed
		}

		void Render() override {
			// Clear background
			glClearColor(0.1F, 0.1F, 0.15F, 1.0F);
			glClear(GL_COLOR_BUFFER_BIT);

			// Sort shapes by zIndex when needed
			if (m_shapesNeedSorting) {
				std::stable_sort(m_shapes.begin(), m_shapes.end(),
								 [](const auto& a, const auto& b) { return a->zIndex < b->zIndex; });
				m_shapesNeedSorting = false;
			}

			// Render all shapes, setting RenderContext for each
			for (auto& shape : m_shapes) {
				UI::RenderContext::SetZIndex(shape->zIndex);
				shape->Render();
			}
		}

		void OnExit() override {
			m_shapes.clear();
		}

		std::string ExportState() override {
			char buf[256];
			snprintf(
				buf,
				sizeof(buf),
				R"({"scene": "layer", "description": "Component hierarchy showcase", "shape_count": %zu})",
				m_shapes.size()
			);
			return {buf};
		}

		const char* GetName() const override { return "layer"; }

	  private:
		std::vector<std::unique_ptr<UI::IComponent>> m_shapes;
		bool m_shapesNeedSorting{true};  // Sort on first render
	};

	// Register scene with SceneManager
	bool g_registered = []() {
		engine::SceneManager::Get().RegisterScene("layer", []() { return std::make_unique<LayerScene>(); });
		return true;
	}();

} // anonymous namespace
