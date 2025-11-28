// Text Shapes Scene - Text Shape API Demonstration
// Demonstrates UI::Text shapes with various styles, sizes, and alignments

#include <graphics/color.h>
#include <primitives/primitives.h>
#include <scene/scene.h>
#include <scene/scene_manager.h>
#include <shapes/shapes.h>
#include <utils/log.h>

#include <GL/glew.h>
#include <memory>
#include <string>
#include <vector>

namespace {

	class TextShapesScene : public engine::IScene {
	  public:
		void OnEnter() override {
			using namespace UI;
			using namespace Foundation;

			// Title
			m_shapes.push_back(std::make_unique<Text>(Text::Args{
				.position = {50.0F, 50.0F},
				.text = "Text Shape Demonstration",
				.style = {.color = Color::White(), .fontSize = 32.0F},
				.id = "title"
			}));

			// Font Size Examples
			m_shapes.push_back(std::make_unique<Text>(Text::Args{
				.position = {50.0F, 120.0F},
				.text = "Font Sizes:",
				.style = {.color = Color(0.7F, 0.7F, 0.7F, 1.0F), .fontSize = 20.0F},
				.id = "size_label"
			}));

			float		yOffset = 160.0F;
			const float sizes[] = {12.0F, 16.0F, 20.0F, 24.0F, 32.0F};
			for (float size : sizes) {
				m_shapes.push_back(std::make_unique<Text>(Text::Args{
					.position = {50.0F, yOffset},
					.text = "Text at " + std::to_string(static_cast<int>(size)) + "px",
					.style = {.color = Color::White(), .fontSize = size}
				}));
				yOffset += size + 10.0F;
			}

			// Color Examples
			m_shapes.push_back(std::make_unique<Text>(Text::Args{
				.position = {400.0F, 120.0F},
				.text = "Colors:",
				.style = {.color = Color(0.7F, 0.7F, 0.7F, 1.0F), .fontSize = 20.0F},
				.id = "color_label"
			}));

			m_shapes.push_back(std::make_unique<Text>(Text::Args{
				.position = {400.0F, 160.0F},
				.text = "Red Text",
				.style = {.color = Color::Red(), .fontSize = 18.0F},
				.id = "red"
			}));

			m_shapes.push_back(std::make_unique<Text>(Text::Args{
				.position = {400.0F, 190.0F},
				.text = "Green Text",
				.style = {.color = Color::Green(), .fontSize = 18.0F},
				.id = "green"
			}));

			m_shapes.push_back(std::make_unique<Text>(Text::Args{
				.position = {400.0F, 220.0F},
				.text = "Blue Text",
				.style = {.color = Color::Blue(), .fontSize = 18.0F},
				.id = "blue"
			}));

			m_shapes.push_back(std::make_unique<Text>(Text::Args{
				.position = {400.0F, 250.0F},
				.text = "Yellow Text",
				.style = {.color = Color::Yellow(), .fontSize = 18.0F},
				.id = "yellow"
			}));

			// Bounding Box Alignment - 3x3 Grid
			m_shapes.push_back(std::make_unique<Text>(Text::Args{
				.position = {700.0F, 50.0F},
				.text = "Bounding Box Alignment (3x3 Grid):",
				.style = {.color = Color(0.7F, 0.7F, 0.7F, 1.0F), .fontSize = 20.0F},
				.id = "align_grid_label"
			}));

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
					float xPos = startX + static_cast<float>(col) * (boxWidth + gap);
					float yPos = startY + static_cast<float>(row) * (boxHeight + gap);

					// Draw bounding box border
					m_shapes.push_back(std::make_unique<Rectangle>(Rectangle::Args{
						.position = {xPos, yPos},
						.size = {boxWidth, boxHeight},
						.style = {
							.fill = Color(0.2F, 0.2F, 0.25F, 1.0F),
							.border = BorderStyle{.color = Color(0.5F, 0.5F, 0.5F, 1.0F), .width = 2.0F}
						}
					}));

					// Create text with bounding box alignment
					m_shapes.push_back(std::make_unique<Text>(Text::Args{
						.position = {xPos, yPos},
						.width = boxWidth,
						.height = boxHeight,
						.text = "TEXT",
						.style = {.color = Color::White(), .fontSize = 24.0F, .hAlign = hAligns[col], .vAlign = vAligns[row]}
					}));
				}
			}

			// Point-Based Alignment - 3x3 Grid (no width/height)
			m_shapes.push_back(std::make_unique<Text>(Text::Args{
				.position = {50.0F, 380.0F},
				.text = "Point-Based Alignment (3x3 Grid):",
				.style = {.color = Color(0.7F, 0.7F, 0.7F, 1.0F), .fontSize = 20.0F},
				.id = "align_point_label"
			}));

			// Grid configuration for point-based alignment
			const float spacing = 100.0F;
			const float pointStartX = 150.0F;
			const float pointStartY = 440.0F;

			// Create 3x3 grid of text with origin markers (no bounding boxes)
			for (int row = 0; row < 3; row++) {
				for (int col = 0; col < 3; col++) {
					float xPos = pointStartX + static_cast<float>(col) * spacing;
					float yPos = pointStartY + static_cast<float>(row) * spacing;

					// Draw small red circle at the origin point
					m_shapes.push_back(std::make_unique<Circle>(Circle::Args{
						.center = {xPos, yPos},
						.radius = 4.0F,
						.style = {.fill = Color::Red()}
					}));

					// Create text with point-based alignment (NO width/height)
					m_shapes.push_back(std::make_unique<Text>(Text::Args{
						.position = {xPos, yPos},
						.text = "TEXT",
						.style = {.color = Color::White(), .fontSize = 24.0F, .hAlign = hAligns[col], .vAlign = vAligns[row]}
					}));
				}
			}

			LOG_INFO(UI, "Text shapes scene initialized with {} shapes", m_shapes.size());
		}

		void HandleInput(float /*dt*/) override {
			// No input handling needed
		}

		void Update(float /*dt*/) override {
			// Static scene - no updates needed
		}

		void Render() override {
			// Clear background
			glClearColor(0.1F, 0.1F, 0.12F, 1.0F);
			glClear(GL_COLOR_BUFFER_BIT);

			// Render all shapes
			for (auto& shape : m_shapes) {
				shape->Render();
			}
		}

		void OnExit() override {
			m_shapes.clear();
		}

		std::string ExportState() override {
			return R"({"scene": "text_shapes", "description": "Text shape API demonstration"})";
		}

		const char* GetName() const override { return "text_shapes"; }

	  private:
		std::vector<std::unique_ptr<UI::IComponent>> m_shapes;
	};

	// Register scene with SceneManager
	bool g_registered = []() {
		engine::SceneManager::Get().RegisterScene("text_shapes", []() { return std::make_unique<TextShapesScene>(); });
		return true;
	}();

} // anonymous namespace
