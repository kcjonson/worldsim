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

			// Create root container (pure hierarchy node - no visual)
			// No zIndex needed - will auto-assign based on insertion order
			Container rootContainer{.id = "root_container"};
			m_rootLayer = m_layerManager.Create(rootContainer);

			// Create child layers - they'll render in insertion order automatically
			// No need to specify zIndex unless you want explicit ordering

			// Background layer (auto zIndex = 2.0)
			Rectangle bgRect{
				.position = {100.0F, 100.0F},
				.size = {600.0F, 400.0F},
				.style = {.fill = Color(0.15F, 0.15F, 0.2F, 1.0F)},
				.id = "background"
			};
			uint32_t bgLayer = m_layerManager.AddChild(m_rootLayer, bgRect);

			// Rectangle layer (auto zIndex = 3.0)
			Rectangle rect1{
				.position = {150.0F, 150.0F},
				.size = {200.0F, 150.0F},
				.style = {.fill = Color::Red(), .border = BorderStyle{.color = Color::White(), .width = 3.0F}},
				.id = "red_rect"
			};
			m_layerManager.AddChild(bgLayer, rect1);

			// Circle layer (auto zIndex = 4.0) - overlaps rectangle
			Circle circle{.center = {400.0F, 250.0F}, .radius = 80.0F, .color = Color::Blue(), .id = "blue_circle"};
			m_layerManager.AddChild(bgLayer, circle);

			// Line layer (auto zIndex = 5.0) - crosses other shapes
			Line line{
				.start = {150.0F, 150.0F}, .end = {500.0F, 400.0F}, .style = {.color = Color::Green(), .width = 4.0F}, .id = "diagonal_line"
			};
			m_layerManager.AddChild(bgLayer, line);

			// Text layer (auto zIndex = 6.0) - on top
			Text text{.position = {200.0F, 180.0F}, .text = "Layer System Demo", .color = Color::Yellow(), .id = "title_text"};
			m_layerManager.AddChild(bgLayer, text);

			// Nested hierarchy demonstration - sidebar with children
			Rectangle sidebar{
				.position = {550.0F, 150.0F},
				.size = {150.0F, 350.0F},
				.style = {.fill = Color(0.3F, 0.3F, 0.35F, 1.0F), .border = BorderStyle{.color = Color::Cyan(), .width = 2.0F}},
				.id = "sidebar"
			};
			uint32_t sidebarLayer = m_layerManager.AddChild(bgLayer, sidebar);

			// Sidebar items (buttons) - auto zIndex maintains insertion order
			for (int i = 0; i < 5; i++) {
				auto const iFloat = static_cast<float>(i);
				Rectangle  button{
					 .position = {560.0F, 170.0F + (iFloat * 60.0F)},
					 .size = {130.0F, 50.0F},
					 .style = {.fill = Color(0.4F, 0.4F, 0.45F, 1.0F), .border = BorderStyle{.color = Color::White(), .width = 1.0F}},
					 .id = nullptr
				 };
				uint32_t buttonLayer = m_layerManager.AddChild(sidebarLayer, button);

				// Button label (renders on top due to insertion order)
				Text buttonText{
					.position = {570.0F, 190.0F + (iFloat * 60.0F)},
					.text = "Button " + std::to_string(i + 1),
					.color = Color::White(),
					.id = nullptr
				};
				m_layerManager.AddChild(buttonLayer, buttonText);
			}

			// Z-ordering demonstration - overlapping rectangles (insertion order = render order)
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
				m_layerManager.AddChild(bgLayer, overlapRect);
			}

			// Explicit zIndex override demonstration - reverse the render order
			// Added in order 0,1,2,3 but with zIndex 30,20,10,0 = renders as blue,green,orange,red (reversed!)
			for (int i = 0; i < 4; i++) {
				float offset = static_cast<float>(i) * 30.0F;
				float hue = static_cast<float>(i) / 4.0F;
				Color color(hue, 0.5F, 1.0F - hue, 0.8F);

				Rectangle reverseRect{
					.position = {450.0F + offset, 350.0F + offset},
					.size = {100.0F, 100.0F},
					.style = {.fill = color, .border = BorderStyle{.color = Color::White(), .width = 2.0F}},
					.zIndex = 30.0F - (static_cast<float>(i) * 10.0F), // 30, 20, 10, 0 (explicit reverse)
					.id = nullptr
				};
				m_layerManager.AddChild(bgLayer, reverseRect);
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
