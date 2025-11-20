// Text Shapes Scene - Text Shape API Demonstration
// Demonstrates UI::Text shapes with various styles, sizes, and alignments

#include <font/font_renderer.h>
#include <font/text_batch_renderer.h>
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

			// Initialize font renderer for text shapes
			m_fontRenderer = std::make_unique<ui::FontRenderer>();
			if (!m_fontRenderer->Initialize()) {
				LOG_ERROR(UI, "Failed to initialize FontRenderer!");
				return;
			}

			// Set up projection matrix for text rendering
			int viewportWidth = 0;
			int viewportHeight = 0;
			Renderer::Primitives::GetViewport(viewportWidth, viewportHeight);
			glm::mat4 projection = glm::ortho(0.0F, static_cast<float>(viewportWidth), static_cast<float>(viewportHeight), 0.0F);
			m_fontRenderer->SetProjectionMatrix(projection);

			// Set font renderer in Primitives API so Text shapes can use it
			Renderer::Primitives::SetFontRenderer(m_fontRenderer.get());

			// Initialize text batch renderer for batched SDF text rendering
			m_textBatchRenderer = std::make_unique<ui::TextBatchRenderer>();
			m_textBatchRenderer->Initialize(m_fontRenderer.get());
			m_textBatchRenderer->SetProjectionMatrix(projection);
			Renderer::Primitives::SetTextBatchRenderer(m_textBatchRenderer.get());

			LOG_INFO(UI, "FontRenderer and TextBatchRenderer initialized for text shapes scene");

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

			// Horizontal Alignment Examples
			Text hAlignLabel{
				.position = {50.0F, 380.0F},
				.text = "Horizontal Alignment:",
				.style = {.color = Color(0.7F, 0.7F, 0.7F, 1.0F), .fontSize = 20.0F},
				.id = "halign_label"
			};
			m_layerManager.AddChild(m_rootLayer, hAlignLabel);

			// Draw reference line
			Rectangle refLine{
				.position = {350.0F, 420.0F}, .size = {2.0F, 100.0F}, .style = {.fill = Color(0.5F, 0.5F, 0.5F, 1.0F)}, .id = "ref_line"
			};
			m_layerManager.AddChild(m_rootLayer, refLine);

			Text leftAlign{
				.position = {350.0F, 430.0F},
				.text = "Left aligned",
				.style = {.color = Color::White(), .fontSize = 16.0F, .hAlign = HorizontalAlign::Left},
				.id = "left_align"
			};
			m_layerManager.AddChild(m_rootLayer, leftAlign);

			Text centerAlign{
				.position = {350.0F, 460.0F},
				.text = "Center aligned",
				.style = {.color = Color::White(), .fontSize = 16.0F, .hAlign = HorizontalAlign::Center},
				.id = "center_align"
			};
			m_layerManager.AddChild(m_rootLayer, centerAlign);

			Text rightAlign{
				.position = {350.0F, 490.0F},
				.text = "Right aligned",
				.style = {.color = Color::White(), .fontSize = 16.0F, .hAlign = HorizontalAlign::Right},
				.id = "right_align"
			};
			m_layerManager.AddChild(m_rootLayer, rightAlign);

			// Vertical Alignment Examples
			Text vAlignLabel{
				.position = {600.0F, 380.0F},
				.text = "Vertical Alignment:",
				.style = {.color = Color(0.7F, 0.7F, 0.7F, 1.0F), .fontSize = 20.0F},
				.id = "valign_label"
			};
			m_layerManager.AddChild(m_rootLayer, vAlignLabel);

			// Draw reference line
			Rectangle vRefLine{
				.position = {600.0F, 470.0F}, .size = {300.0F, 2.0F}, .style = {.fill = Color(0.5F, 0.5F, 0.5F, 1.0F)}, .id = "vref_line"
			};
			m_layerManager.AddChild(m_rootLayer, vRefLine);

			Text topAlign{
				.position = {610.0F, 470.0F},
				.text = "Top",
				.style = {.color = Color::White(), .fontSize = 16.0F, .vAlign = VerticalAlign::Top},
				.id = "top_align"
			};
			m_layerManager.AddChild(m_rootLayer, topAlign);

			Text middleAlign{
				.position = {700.0F, 470.0F},
				.text = "Middle",
				.style = {.color = Color::White(), .fontSize = 16.0F, .vAlign = VerticalAlign::Middle},
				.id = "middle_align"
			};
			m_layerManager.AddChild(m_rootLayer, middleAlign);

			Text bottomAlign{
				.position = {810.0F, 470.0F},
				.text = "Bottom",
				.style = {.color = Color::White(), .fontSize = 16.0F, .vAlign = VerticalAlign::Bottom},
				.id = "bottom_align"
			};
			m_layerManager.AddChild(m_rootLayer, bottomAlign);
		}

		void HandleInput(float dt) override {
			// No input handling needed
		}

		void Update(float dt) override { m_layerManager.UpdateAll(dt); }

		void Render() override {
			// Clear background
			glClearColor(0.1F, 0.1F, 0.15F, 1.0F);
			glClear(GL_COLOR_BUFFER_BIT);

			// Render all text shapes
			m_layerManager.RenderAll();
		}

		void OnExit() override {
			m_layerManager.Clear();
			Renderer::Primitives::SetTextBatchRenderer(nullptr);
			Renderer::Primitives::SetFontRenderer(nullptr);
			m_textBatchRenderer.reset();
			m_fontRenderer.reset();
		}

		std::string ExportState() override { return R"({"scene": "text_shapes", "description": "Text shape API demonstration"})"; }

		const char* GetName() const override { return "text_shapes"; }

	  private:
		UI::LayerManager				  m_layerManager;
		uint32_t						  m_rootLayer{0};
		std::unique_ptr<ui::FontRenderer> m_fontRenderer;
		std::unique_ptr<ui::TextBatchRenderer> m_textBatchRenderer;
	};

	// Register scene with SceneManager
	bool g_registered = []() {
		engine::SceneManager::Get().RegisterScene("text_shapes", []() { return std::make_unique<TextShapesScene>(); });
		return true;
	}();

} // anonymous namespace
