// Minimal SDF Test Scene - Single line of text to verify SDF rendering works

#include <font/font_renderer.h>
#include <font/text_batch_renderer.h>
#include <primitives/primitives.h>
#include <scene/scene.h>
#include <scene/scene_manager.h>
#include <utils/log.h>

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>

namespace {

	class SDFMinimalScene : public engine::IScene {
	  public:
		void OnEnter() override {
			LOG_INFO(UI, "=== SDF Minimal Test Scene ===");

			// Initialize FontRenderer
			m_fontRenderer = std::make_unique<ui::FontRenderer>();
			if (!m_fontRenderer->Initialize()) {
				LOG_ERROR(UI, "Failed to initialize FontRenderer");
				return;
			}

			// Initialize TextBatchRenderer
			m_textBatchRenderer = std::make_unique<ui::TextBatchRenderer>();
			if (!m_textBatchRenderer->Initialize(m_fontRenderer.get())) {
				LOG_ERROR(UI, "Failed to initialize TextBatchRenderer");
				return;
			}

			// IMPORTANT: Use LOGICAL pixels (window size), not PHYSICAL pixels (framebuffer size)
			// On Retina displays, GL_VIEWPORT returns physical pixels (2x larger)
			// But text coordinates and BatchRenderer primitives use logical pixels
			// For consistency, we use the same logical dimensions as CoordinateSystem
			//
			// TODO: Make TextBatchRenderer use CoordinateSystem directly like BatchRenderer
			int logicalWidth = 1344; // Logical pixel width (half of 2688 physical pixels on Retina)
			int logicalHeight = 840; // Logical pixel height (half of 1680 physical pixels on Retina)

			LOG_INFO(UI, "Setting projection for logical viewport: %dx%d", logicalWidth, logicalHeight);

			// CRITICAL: glm::ortho() returns all zeros (rows 0 and 1) in this build environment!
			// Manually construct orthographic projection matrix
			// Formula for ortho(left, right, bottom, top, near, far):
			//   Matrix[0][0] = 2 / (right - left)
			//   Matrix[1][1] = 2 / (top - bottom)
			//   Matrix[2][2] = -2 / (far - near)
			//   Matrix[3][0] = -(right + left) / (right - left)
			//   Matrix[3][1] = -(top + bottom) / (top - bottom)
			//   Matrix[3][2] = -(far + near) / (far - near)

			float left = 0.0f;
			float right = static_cast<float>(logicalWidth);	  // 1344
			float bottom = static_cast<float>(logicalHeight); // 840
			float top = 0.0f;
			float near = -1.0f;
			float far = 1.0f;

			glm::mat4 projection(1.0f);							 // Start with identity
			projection[0][0] = 2.0f / (right - left);			 // Standard X-axis scaling
			projection[1][1] = 2.0f / (top - bottom);			 // 2/(0-840) = -0.002380...
			projection[2][2] = -2.0f / (far - near);			 // -2/2 = -1.0
			projection[3][0] = -(right + left) / (right - left); // -1344/1344 = -1.0
			projection[3][1] = -(top + bottom) / (top - bottom); // -840/-840 = 1.0
			projection[3][2] = -(far + near) / (far - near);	 // 0/2 = 0.0

			LOG_INFO(UI, "Manual projection matrix:");
			LOG_INFO(UI, "  [0]: %.6f, %.6f, %.6f, %.6f", projection[0][0], projection[0][1], projection[0][2], projection[0][3]);
			LOG_INFO(UI, "  [1]: %.6f, %.6f, %.6f, %.6f", projection[1][0], projection[1][1], projection[1][2], projection[1][3]);
			LOG_INFO(UI, "  [2]: %.6f, %.6f, %.6f, %.6f", projection[2][0], projection[2][1], projection[2][2], projection[2][3]);
			LOG_INFO(UI, "  [3]: %.6f, %.6f, %.6f, %.6f", projection[3][0], projection[3][1], projection[3][2], projection[3][3]);

			m_textBatchRenderer->SetProjectionMatrix(projection);

			LOG_INFO(UI, "SDF Minimal Scene initialized");
		}

		void HandleInput(float dt) override {}

		void Update(float dt) override {}

		void Render() override {
			// Clear to dark blue
			glClearColor(0.1f, 0.1f, 0.2f, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT);

			float	  scale = 2.0f;					 // 2.0 = 32px (base is 16px)
			glm::vec4 color(1.0f, 1.0f, 1.0f, 1.0f); // White
			float	  zIndex = 0.0f;

			// Render uppercase alphabet
			glm::vec2 position1(50.0f, 150.0f);
			m_textBatchRenderer->AddText("ABCDEFGHIJKLMNOPQRSTUVWXYZ", position1, scale, color, zIndex);

			// Render lowercase alphabet
			glm::vec2 position2(50.0f, 250.0f);
			m_textBatchRenderer->AddText("abcdefghijklmnopqrstuvwxyz", position2, scale, color, zIndex);

			// Render numbers for reference
			glm::vec2 position3(50.0f, 350.0f);
			m_textBatchRenderer->AddText("0123456789", position3, scale, color, zIndex);

			// Flush to render
			m_textBatchRenderer->Flush();
		}

		void OnExit() override {
			m_textBatchRenderer.reset();
			m_fontRenderer.reset();
		}

		std::string ExportState() override { return R"({"scene": "sdf_minimal", "description": "Minimal SDF rendering test"})"; }

		const char* GetName() const override { return "sdf_minimal"; }

	  private:
		std::unique_ptr<ui::FontRenderer>	   m_fontRenderer;
		std::unique_ptr<ui::TextBatchRenderer> m_textBatchRenderer;
	};

	// Register scene
	bool g_registered = []() {
		engine::SceneManager::Get().RegisterScene("sdf_minimal", []() { return std::make_unique<SDFMinimalScene>(); });
		return true;
	}();

} // anonymous namespace
