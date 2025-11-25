// Text Shapes Scene - Text Shape API Demonstration
// Demonstrates UI::Text shapes with various styles, sizes, and alignments

#include <graphics/color.h>
#include <layer/layer_manager.h>
#include <primitives/primitives.h>
#include <scene/scene.h>
#include <scene/scene_manager.h>
#include <utils/log.h>

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include <string>

namespace {

	class TextShapesScene : public engine::IScene {
	  public:
		void OnEnter() override {
			using namespace UI;
			using namespace Foundation;

			// NOTE: FontRenderer and TextBatchRenderer are initialized globally in main.cpp
			// No per-scene setup required!

			// Create root container
			Container rootContainer{.id = "root"};
			m_rootLayer = m_layerManager.Create(rootContainer);

			// Title
			Text title{
				.position = {50.0F, 50.0F},
				.text = "Text Shape Demonstration",
				.style = {.color = Color::White(), .fontSize = 32.0F},
				.id = "title"
			};
			m_layerManager.AddChild(m_rootLayer, title);

			// Font Size Examples
			Text sizeLabel{
				.position = {50.0F, 120.0F},
				.text = "Font Sizes:",
				.style = {.color = Color(0.7F, 0.7F, 0.7F, 1.0F), .fontSize = 20.0F},
				.id = "size_label"
			};
			m_layerManager.AddChild(m_rootLayer, sizeLabel);

			float		yOffset = 160.0F;
			const float sizes[] = {12.0F, 16.0F, 20.0F, 24.0F, 32.0F};
			for (float size : sizes) {
				Text sizeExample{
					.position = {50.0F, yOffset},
					.text = "Text at " + std::to_string(static_cast<int>(size)) + "px",
					.style = {.color = Color::White(), .fontSize = size},
					.zIndex = 1.0F,
					.id = nullptr
				};
				m_layerManager.AddChild(m_rootLayer, sizeExample);
				yOffset += size + 10.0F;
			}

			// Color Examples
			Text colorLabel{
				.position = {400.0F, 120.0F},
				.text = "Colors:",
				.style = {.color = Color(0.7F, 0.7F, 0.7F, 1.0F), .fontSize = 20.0F},
				.id = "color_label"
			};
			m_layerManager.AddChild(m_rootLayer, colorLabel);

			Text redText{
				.position = {400.0F, 160.0F}, .text = "Red Text", .style = {.color = Color::Red(), .fontSize = 18.0F}, .id = "red"
			};
			m_layerManager.AddChild(m_rootLayer, redText);

			Text greenText{
				.position = {400.0F, 190.0F}, .text = "Green Text", .style = {.color = Color::Green(), .fontSize = 18.0F}, .id = "green"
			};
			m_layerManager.AddChild(m_rootLayer, greenText);

			Text blueText{
				.position = {400.0F, 220.0F}, .text = "Blue Text", .style = {.color = Color::Blue(), .fontSize = 18.0F}, .id = "blue"
			};
			m_layerManager.AddChild(m_rootLayer, blueText);

			Text yellowText{
				.position = {400.0F, 250.0F}, .text = "Yellow Text", .style = {.color = Color::Yellow(), .fontSize = 18.0F}, .id = "yellow"
			};
			m_layerManager.AddChild(m_rootLayer, yellowText);

			// Bounding Box Alignment - 3x3 Grid
			Text alignGridLabel{
				.position = {700.0F, 50.0F},
				.text = "Bounding Box Alignment (3x3 Grid):",
				.style = {.color = Color(0.7F, 0.7F, 0.7F, 1.0F), .fontSize = 20.0F},
				.id = "align_grid_label"
			};
			m_layerManager.AddChild(m_rootLayer, alignGridLabel);

			// Grid configuration
			constexpr float		  boxWidth = 180.0F;
			constexpr float		  boxHeight = 120.0F;
			constexpr float		  gap = 20.0F;
			constexpr float		  startX = 700.0F;
			constexpr float		  startY = 90.0F;
			const HorizontalAlign hAligns[] = {HorizontalAlign::Left, HorizontalAlign::Center, HorizontalAlign::Right};
			const VerticalAlign	  vAligns[] = {VerticalAlign::Top, VerticalAlign::Middle, VerticalAlign::Bottom};

			// Create 3x3 grid of text boxes with visible bounding boxes
			for (int row = 0; row < 3; row++) {
				for (int col = 0; col < 3; col++) {
					float xPos = startX + col * (boxWidth + gap);
					float yPos = startY + row * (boxHeight + gap);

					// Draw bounding box border
					Rectangle boundingBox{
						.position = {xPos, yPos},
						.size = {boxWidth, boxHeight},
						.style =
							{.fill = Color(0.2F, 0.2F, 0.25F, 1.0F),
							 .border = BorderStyle{.color = Color(0.5F, 0.5F, 0.5F, 1.0F), .width = 2.0F}},
						.zIndex = 1.0F,
						.id = nullptr
					};
					m_layerManager.AddChild(m_rootLayer, boundingBox);

					// Create text with bounding box (no alignment set)
					Text alignedText{
						.position = {xPos, yPos},
						.width = boxWidth,
						.height = boxHeight,
						.text = "TEXT",
						.style = {.color = Color::White(), .fontSize = 24.0F},
						.zIndex = 2.0F,
						.id = nullptr
					};
					m_layerManager.AddChild(m_rootLayer, alignedText);
				}
			}

			// Point-Based Alignment - 3x3 Grid (no width/height)
			Text alignGridLabel2{
				.position = {50.0F, 380.0F},
				.text = "Point-Based Alignment (3x3 Grid):",
				.style = {.color = Color(0.7F, 0.7F, 0.7F, 1.0F), .fontSize = 20.0F},
				.id = "align_point_label"
			};
			m_layerManager.AddChild(m_rootLayer, alignGridLabel2);

			// Grid configuration for point-based alignment
			const float spacing = 100.0F;
			const float pointStartX = 150.0F;
			const float pointStartY = 440.0F;

			// Create 3x3 grid of text with origin markers (no bounding boxes)
			for (int row = 0; row < 3; row++) {
				for (int col = 0; col < 3; col++) {
					float xPos = pointStartX + col * spacing;
					float yPos = pointStartY + row * spacing;

					// Draw small red circle at the origin point
					Circle originMarker{
						.center = {xPos, yPos}, .radius = 4.0F, .style = {.fill = Color::Red()}, .zIndex = 3.0F, .id = nullptr
					};
					m_layerManager.AddChild(m_rootLayer, originMarker);

					// Create text with point-based alignment (NO width/height)
					Text pointText{
						.position = {xPos, yPos},
						.text = "TEXT",
						.style = {.color = Color::White(), .fontSize = 24.0F, .hAlign = hAligns[col], .vAlign = vAligns[row]},
						.zIndex = 2.0F,
						.id = nullptr
					};
					m_layerManager.AddChild(m_rootLayer, pointText);
				}
			}
		}

		void HandleInput(float dt) override {
			// No input handling needed
		}

		void Update(float dt) override { m_layerManager.UpdateAll(dt); }

		void Render() override {
			// NOTE: Screen clearing handled by Application main loop
			m_layerManager.RenderAll();
		}

		void OnExit() override {
			m_layerManager.Clear();
			// NOTE: FontRenderer and TextBatchRenderer cleanup handled by main.cpp
		}

		std::string ExportState() override { return R"({"scene": "text_shapes", "description": "Text shape API demonstration"})"; }

		const char* GetName() const override { return "text_shapes"; }

	  private:
		UI::LayerManager m_layerManager;
		uint32_t		 m_rootLayer{0};
	};

	// Register scene with SceneManager
	bool g_registered = []() {
		engine::SceneManager::Get().RegisterScene("text_shapes", []() { return std::make_unique<TextShapesScene>(); });
		return true;
	}();

} // anonymous namespace
