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
				.position = {50.0f, 50.0f},
				.size = {700.0f, 500.0f},
				.style = {.fill = Color(0.2f, 0.2f, 0.25f, 1.0f), .border = BorderStyle{.color = Color::White(), .width = 2.0f}},
				.id = "root_container"
			};
			m_rootLayer = m_layerManager.CreateRectangle(rootRect);
			m_layerManager.SetZIndex(m_rootLayer, 0.0f);

			// Create child layers with different z-indices to demonstrate ordering

			// Background layer (z=1)
			Rectangle bgRect{
				.position = {100.0f, 100.0f},
				.size = {600.0f, 400.0f},
				.style = {.fill = Color(0.15f, 0.15f, 0.2f, 1.0f)},
				.id = "background"
			};
			uint32_t bgLayer = m_layerManager.CreateRectangle(bgRect);
			m_layerManager.SetZIndex(bgLayer, 1.0f);
			m_layerManager.AddChild(m_rootLayer, bgLayer);

			// Rectangle layer (z=2)
			Rectangle rect1{
				.position = {150.0f, 150.0f},
				.size = {200.0f, 150.0f},
				.style = {.fill = Color::Red(), .border = BorderStyle{.color = Color::White(), .width = 3.0f}},
				.id = "red_rect"
			};
			uint32_t rectLayer = m_layerManager.CreateRectangle(rect1);
			m_layerManager.SetZIndex(rectLayer, 2.0f);
			m_layerManager.AddChild(bgLayer, rectLayer);

			// Circle layer (z=3) - overlaps rectangle
			Circle	 circle{.center = {400.0f, 250.0f}, .radius = 80.0f, .color = Color::Blue(), .id = "blue_circle"};
			uint32_t circleLayer = m_layerManager.CreateCircle(circle);
			m_layerManager.SetZIndex(circleLayer, 3.0f);
			m_layerManager.AddChild(bgLayer, circleLayer);

			// Line layer (z=4) - crosses other shapes
			Line line{
				.start = {150.0f, 150.0f}, .end = {500.0f, 400.0f}, .style = {.color = Color::Green(), .width = 4.0f}, .id = "diagonal_line"
			};
			uint32_t lineLayer = m_layerManager.CreateLine(line);
			m_layerManager.SetZIndex(lineLayer, 4.0f);
			m_layerManager.AddChild(bgLayer, lineLayer);

			// Text layer (z=5) - on top
			Text	 text{.position = {200.0f, 180.0f}, .text = "Layer System Demo", .color = Color::Yellow(), .id = "title_text"};
			uint32_t textLayer = m_layerManager.CreateText(text);
			m_layerManager.SetZIndex(textLayer, 5.0f);
			m_layerManager.AddChild(bgLayer, textLayer);

			// Nested hierarchy demonstration - sidebar with children
			Rectangle sidebar{
				.position = {550.0f, 150.0f},
				.size = {150.0f, 350.0f},
				.style = {.fill = Color(0.3f, 0.3f, 0.35f, 1.0f), .border = BorderStyle{.color = Color::Cyan(), .width = 2.0f}},
				.id = "sidebar"
			};
			uint32_t sidebarLayer = m_layerManager.CreateRectangle(sidebar);
			m_layerManager.SetZIndex(sidebarLayer, 2.0f);
			m_layerManager.AddChild(bgLayer, sidebarLayer);

			// Sidebar items (buttons)
			for (int i = 0; i < 5; i++) {
				Rectangle button{
					.position = {560.0f, 170.0f + (i * 60.0f)},
					.size = {130.0f, 50.0f},
					.style = {.fill = Color(0.4f, 0.4f, 0.45f, 1.0f), .border = BorderStyle{.color = Color::White(), .width = 1.0f}},
					.id = nullptr
				};
				uint32_t buttonLayer = m_layerManager.CreateRectangle(button);
				m_layerManager.SetZIndex(buttonLayer, static_cast<float>(i));
				m_layerManager.AddChild(sidebarLayer, buttonLayer);

				// Button label
				Text buttonText{
					.position = {570.0f, 190.0f + (i * 60.0f)},
					.text = "Button " + std::to_string(i + 1),
					.color = Color::White(),
					.id = nullptr
				};
				uint32_t textLayer = m_layerManager.CreateText(buttonText);
				m_layerManager.SetZIndex(textLayer, 1.0f);
				m_layerManager.AddChild(buttonLayer, textLayer);
			}

			// Z-ordering demonstration - overlapping rectangles
			for (int i = 0; i < 4; i++) {
				float offset = static_cast<float>(i) * 30.0f;
				float hue = static_cast<float>(i) / 4.0f;
				Color color(hue, 1.0f - hue, 0.5f, 0.8f);

				Rectangle overlapRect{
					.position = {150.0f + offset, 350.0f + offset},
					.size = {100.0f, 100.0f},
					.style = {.fill = color, .border = BorderStyle{.color = Color::White(), .width = 2.0f}},
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
