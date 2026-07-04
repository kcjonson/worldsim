// Measurement/rendering consistency tests for FontRenderer.
//
// MeasureText and generateGlyphQuads must agree on advances (one shared
// iteration), or centered/right-aligned text drifts against its measured box.
// The key assertion renders a doubled string (s+s): the second copy's quads
// must sit exactly MeasureText(s) + letterSpacing to the right of the first
// copy's, which proves measured width == rendered pen advance, including the
// '?' fallback for characters missing from the atlas.
//
// Requires a real GL context (atlas textures are uploaded on load) and the
// fonts/ resources; skips gracefully when either is unavailable (headless CI).

#include "font/FontRenderer.h"
#include "utils/ResourcePath.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace ui {

	class FontRendererTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			if (glfwInit() != GLFW_TRUE) {
				GTEST_SKIP() << "GLFW initialization failed (no display available)";
			}
			glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
			window = glfwCreateWindow(32, 32, "font-test", nullptr, nullptr);
			if (window == nullptr) {
				glfwTerminate();
				GTEST_SKIP() << "Could not create GLFW window (no OpenGL context available)";
			}
			glfwMakeContextCurrent(window);
			if (glewInit() != GLEW_OK) {
				glfwDestroyWindow(window);
				window = nullptr;
				glfwTerminate();
				GTEST_SKIP() << "GLEW initialization failed";
			}
			if (Foundation::findResourceString("fonts/Roboto-SDF.json").empty()) {
				GTEST_SKIP() << "Font atlas resources not found (run from the repo root)";
			}
			fontRenderer = std::make_unique<FontRenderer>();
			ASSERT_TRUE(fontRenderer->Initialize());
		}

		void TearDown() override {
			fontRenderer.reset(); // Destroy GL textures while the context is current
			if (window != nullptr) {
				glfwDestroyWindow(window);
				glfwTerminate();
			}
		}

		std::vector<FontRenderer::GlyphQuad> quadsFor(const std::string& text, float scale, float letterSpacing) const {
			std::vector<FontRenderer::GlyphQuad> quads;
			fontRenderer->generateGlyphQuads(
				text, glm::vec2(0.0F, 0.0F), scale, glm::vec4(1.0F), quads, Renderer::FontFamily::Roboto, letterSpacing
			);
			return quads;
		}

		GLFWwindow*					  window = nullptr;
		std::unique_ptr<FontRenderer> fontRenderer;
	};

	TEST_F(FontRendererTest, MeasuredWidthMatchesRenderedAdvance) {
		// Includes '\x01', a character absent from the atlas (exercises the '?'
		// fallback inside the doubled-string advance check).
		const std::string missingChar = std::string("mi") + '\x01' + "ssing";
		const std::string testStrings[] = {"Hello, World!", "iiiillll", "A B C", missingChar};
		const float		  letterSpacings[] = {0.0F, 2.5F};
		const float		  scales[] = {1.0F, 1.25F};

		for (const std::string& text : testStrings) {
			for (float letterSpacing : letterSpacings) {
				for (float scale : scales) {
					const float measured = fontRenderer->MeasureText(text, scale, Renderer::FontFamily::Roboto, letterSpacing).x;

					const auto quadsSingle = quadsFor(text, scale, letterSpacing);
					const auto quadsDoubled = quadsFor(text + text, scale, letterSpacing);
					ASSERT_EQ(quadsDoubled.size(), quadsSingle.size() * 2)
						<< "text=" << text << " ls=" << letterSpacing << " scale=" << scale;

					// The second copy starts one measured width (plus the gap's
					// letter-spacing) after the first.
					const float expectedOffset = measured + letterSpacing;
					for (size_t i = 0; i < quadsSingle.size(); ++i) {
						const auto& first = quadsSingle[i];
						const auto& second = quadsDoubled[i + quadsSingle.size()];
						EXPECT_NEAR(second.position.x - first.position.x, expectedOffset, 1e-3F)
							<< "glyph " << i << " text=" << text << " ls=" << letterSpacing << " scale=" << scale;
						EXPECT_FLOAT_EQ(second.position.y, first.position.y);
						EXPECT_FLOAT_EQ(second.size.x, first.size.x);
						EXPECT_FLOAT_EQ(second.size.y, first.size.y);
					}
				}
			}
		}
	}

	TEST_F(FontRendererTest, MissingGlyphMeasuresAndRendersAsFallback) {
		const std::string withMissing = std::string("a") + '\x01' + "b";
		const std::string withFallback = "a?b";

		EXPECT_FLOAT_EQ(fontRenderer->MeasureText(withMissing, 1.0F).x, fontRenderer->MeasureText(withFallback, 1.0F).x);

		const auto quadsMissing = quadsFor(withMissing, 1.0F, 0.0F);
		const auto quadsFallback = quadsFor(withFallback, 1.0F, 0.0F);
		ASSERT_EQ(quadsMissing.size(), quadsFallback.size());
		for (size_t i = 0; i < quadsMissing.size(); ++i) {
			EXPECT_FLOAT_EQ(quadsMissing[i].position.x, quadsFallback[i].position.x);
			EXPECT_FLOAT_EQ(quadsMissing[i].position.y, quadsFallback[i].position.y);
			EXPECT_FLOAT_EQ(quadsMissing[i].uvMin.x, quadsFallback[i].uvMin.x);
			EXPECT_FLOAT_EQ(quadsMissing[i].uvMax.x, quadsFallback[i].uvMax.x);
		}
	}

	TEST_F(FontRendererTest, LetterSpacingSitsBetweenGlyphsOnly) {
		const std::string text = "spacing";
		const float		  letterSpacing = 3.0F;
		const float		  base = fontRenderer->MeasureText(text, 1.0F).x;
		const float		  spaced = fontRenderer->MeasureText(text, 1.0F, Renderer::FontFamily::Roboto, letterSpacing).x;
		EXPECT_NEAR(spaced - base, letterSpacing * static_cast<float>(text.size() - 1), 1e-3F);

		EXPECT_FLOAT_EQ(fontRenderer->MeasureText("", 1.0F).x, 0.0F);
	}

	TEST_F(FontRendererTest, GlyphQuadsCarryBaselinePenOrigin) {
		// runOrigin is the pen origin at the baseline: box position + font ascent.
		const float							 ascent = fontRenderer->getAscent(1.0F);
		const glm::vec2						 origin(13.25F, 7.5F);
		std::vector<FontRenderer::GlyphQuad> quads;
		fontRenderer->generateGlyphQuads("Run", origin, 1.0F, glm::vec4(1.0F), quads);
		ASSERT_FALSE(quads.empty());
		for (const auto& quad : quads) {
			EXPECT_FLOAT_EQ(quad.runOrigin.x, origin.x);
			EXPECT_FLOAT_EQ(quad.runOrigin.y, origin.y + ascent);
		}

		// Second call is served from the glyph-quad cache; the origin must still
		// be rewritten per call.
		const glm::vec2						 origin2(101.75F, 44.0F);
		std::vector<FontRenderer::GlyphQuad> cachedQuads;
		fontRenderer->generateGlyphQuads("Run", origin2, 1.0F, glm::vec4(1.0F), cachedQuads);
		ASSERT_EQ(cachedQuads.size(), quads.size());
		for (const auto& quad : cachedQuads) {
			EXPECT_FLOAT_EQ(quad.runOrigin.x, origin2.x);
			EXPECT_FLOAT_EQ(quad.runOrigin.y, origin2.y + ascent);
		}
	}

} // namespace ui
