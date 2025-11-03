// Layer Scene - UI Layer System Showcase
// Demonstrates LayerManager with hierarchy, z-ordering, and all shape types

#include <graphics/color.h>
#include <layer/layer_manager.h>
#include <primitives/primitives.h>
#include <scene/scene.h>
#include <scene/scene_manager.h>

#include <GL/glew.h>

namespace {

	class LayerScene : public engine::IScene {
	  public:
		void OnEnter() override {
			using namespace UI;
			using namespace Foundation;

			// Create root container (large rectangle)
			Rectangle rootRect{
				.position = {50.0F, 50.0F},
				.size = {700.0F, 500.0F},
				.style = {.fill = Color(0.2F, 0.2F, 0.25F, 1.0F), .border = BorderStyle{.color = Color::White(), .width = 2.0F}},
				.id = "root_container"
			};
			m_rootLayer = m_layerManager.CreateRectangle(rootRect);
			m_layerManager.SetZIndex(m_rootLayer, 0.0F);

			// Create child layers with different z-indices to demonstrate ordering

			// Background layer (z=1)
			Rectangle bgRect{
				.position = {100.0F, 100.0F},
				.size = {600.0F, 400.0F},
				.style = {.fill = Color(0.15F, 0.15F, 0.2F, 1.0F)},
				.id = "background"
			};
			uint32_t bgLayer = m_layerManager.CreateRectangle(bgRect);
			m_layerManager.SetZIndex(bgLayer, 1.0F);
			m_layerManager.AddChild(m_rootLayer, bgLayer);

			// Rectangle layer (z=2)
			Rectangle rect1{
				.position = {150.0F, 150.0F},
				.size = {200.0F, 150.0F},
				.style = {.fill = Color::Red(), .border = BorderStyle{.color = Color::White(), .width = 3.0F}},
				.id = "red_rect"
			};
			uint32_t rectLayer = m_layerManager.CreateRectangle(rect1);
			m_layerManager.SetZIndex(rectLayer, 2.0F);
			m_layerManager.AddChild(bgLayer, rectLayer);

			// Circle layer (z=3) - overlaps rectangle
			Circle	 circle{.center = {400.0F, 250.0F}, .radius = 80.0F, .color = Color::Blue(), .id = "blue_circle"};
			uint32_t circleLayer = m_layerManager.CreateCircle(circle);
			m_layerManager.SetZIndex(circleLayer, 3.0F);
			m_layerManager.AddChild(bgLayer, circleLayer);

			// Line layer (z=4) - crosses other shapes
			Line line{
				.start = {150.0F, 150.0F}, .end = {500.0F, 400.0F}, .style = {.color = Color::Green(), .width = 4.0F}, .id = "diagonal_line"
			};
			uint32_t lineLayer = m_layerManager.CreateLine(line);
			m_layerManager.SetZIndex(lineLayer, 4.0F);
			m_layerManager.AddChild(bgLayer, lineLayer);

			// Text layer (z=5) - on top
			Text	 text{.position = {200.0F, 180.0F}, .text = "Layer System Demo", .color = Color::Yellow(), .id = "title_text"};
			uint32_t textLayer = m_layerManager.CreateText(text);
			m_layerManager.SetZIndex(textLayer, 5.0F);
			m_layerManager.AddChild(bgLayer, textLayer);

			// Nested hierarchy demonstration - sidebar with children
			Rectangle sidebar{
				.position = {550.0F, 150.0F},
				.size = {150.0F, 350.0F},
				.style = {.fill = Color(0.3F, 0.3F, 0.35F, 1.0F), .border = BorderStyle{.color = Color::Cyan(), .width = 2.0F}},
				.id = "sidebar"
			};
			uint32_t sidebarLayer = m_layerManager.CreateRectangle(sidebar);
			m_layerManager.SetZIndex(sidebarLayer, 2.0F);
			m_layerManager.AddChild(bgLayer, sidebarLayer);

			// Sidebar items (buttons)
			for (int i = 0; i < 5; i++) {
				auto const iF = static_cast<float>(i);
				Rectangle  button{
					 .position = {560.0F, 170.0F + (iF * 60.0F)},
					 .size = {130.0F, 50.0F},
					 .style = {.fill = Color(0.4F, 0.4F, 0.45F, 1.0F), .border = BorderStyle{.color = Color::White(), .width = 1.0F}},
					 .id = nullptr
				 };
				uint32_t buttonLayer = m_layerManager.CreateRectangle(button);
				m_layerManager.SetZIndex(buttonLayer, iF);
				m_layerManager.AddChild(sidebarLayer, buttonLayer);

				// Button label
				Text buttonText{
					.position = {570.0F, 190.0F + (iF * 60.0F)},
					.text = "Button " + std::to_string(i + 1),
					.color = Color::White(),
					.id = nullptr
				};
				uint32_t textLayer = m_layerManager.CreateText(buttonText);
				m_layerManager.SetZIndex(textLayer, 1.0F);
				m_layerManager.AddChild(buttonLayer, textLayer);
			}

			// Z-ordering demonstration - overlapping rectangles
			for (int i = 0; i < 4; i++) {
				float offset = static_cast<float>(i) * 30.0F;
				float hue = static_cast<float>(i) / 4.0F;
				Color color(hue, 1.0F - hue, 0.5F, 0.8F);

				Rectangle overlapRect{
					.position = {150.0F + offset, 350.0F + offset},
					.size = {100.0F, 100.0F},
					.style = {.fill = color, .border = BorderStyle{.color = Color::White(), .width = 2.0F}},
					.id = nullptr
				};
				uint32_t layer = m_layerManager.CreateRectangle(overlapRect);
				m_layerManager.SetZIndex(layer, static_cast<float>(i));
				m_layerManager.AddChild(bgLayer, layer);
			}
		}

		void HandleInput(float dt) override {
			// No input handling needed - static scene
		}

		void Update(float dt) override {
			// Update layer manager (for animations in future)
			m_layerManager.UpdateAll(dt);
		}

		void Render() override {
			// Clear background
			glClearColor(0.1F, 0.1F, 0.15F, 1.0F);
			glClear(GL_COLOR_BUFFER_BIT);

			// Render entire layer hierarchy
			m_layerManager.RenderAll();
		}

		void OnExit() override {
			// Cleanup
			m_layerManager.Clear();
		}

		std::string ExportState() override {
			// Export layer hierarchy info with actual counts
			char buf[512];
			snprintf(
				buf,
				sizeof(buf),
				R"({"scene": "layer", "description": "UI Layer System showcase", "layer_count": %zu})",
				m_layerManager.GetLayerCount()
			);
			return {buf};
		}

		const char* GetName() const override { return "layer"; }

	  private:
		UI::LayerManager m_layerManager;
		uint32_t		 m_rootLayer{0};
	};

	// Register scene with SceneManager
	bool g_registered = []() {
		engine::SceneManager::Get().RegisterScene("layer", []() { return std::make_unique<LayerScene>(); });
		return true;
	}();

} // anonymous namespace
