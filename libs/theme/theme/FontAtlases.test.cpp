// The shipped font atlases, loaded as the apps' App::create loads them but on the null backend, so no GL. Every
// family has to load its own atlas (one that fails draws with the default, silently), and no glyph
// may sit closer to its cell's edge than its quad reads, or it draws slivers of its neighbours:
// salal's FontRenderer::crampedGlyphs, the check tools/generate_sdf_atlas leaves room for. The
// atlases are the font-atlas target's output in fonts/, which theme-tests depends on.

#include "BaseShader.h"
#include "backend/NullBackend.h"
#include "font/FontRenderer.h"
#include "primitives/Primitives.h"
#include "theme/Fonts.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <vector>

namespace {

	// This file lives at <root>/libs/theme/theme/; kFontAtlases' paths are relative to <root>.
	std::filesystem::path projectRoot() {
		return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
	}

	class FontAtlasesTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			Renderer::Primitives::init(std::make_unique<Renderer::backend::NullBackend>(), base_shader::source());
			fonts = std::make_unique<Renderer::FontRenderer>();
		}

		void TearDown() override {
			fonts.reset(); // Its atlas textures go while the backend is there
			Renderer::Primitives::shutdown();
		}

		std::unique_ptr<Renderer::FontRenderer> fonts;
	};

} // namespace

TEST_F(FontAtlasesTest, EveryFamilyLoadsItsOwnAtlasWithRoomAroundEveryGlyph) {
	std::vector<Renderer::FontAtlasSource> sources;
	for (const Renderer::FontAtlasSource& atlas : theme::kFontAtlases) {
		sources.push_back(
			{.family = atlas.family,
			 .pngPath = (projectRoot() / atlas.pngPath).string(),
			 .jsonPath = (projectRoot() / atlas.jsonPath).string()}
		);
	}
	ASSERT_TRUE(fonts->Initialize(sources, theme::fontDefault));

	const Renderer::TextureHandle defaultAtlas = fonts->getAtlasTexture(theme::fontDefault);
	for (const Renderer::FontAtlasSource& atlas : theme::kFontAtlases) {
		if (atlas.family != theme::fontDefault) {
			EXPECT_NE(fonts->getAtlasTexture(atlas.family), defaultAtlas)
				<< atlas.jsonPath << " didn't load; its family draws with the default";
		}
		EXPECT_EQ(fonts->crampedGlyphs(atlas.family), "") << atlas.jsonPath << ": these glyphs read past their cells";
	}
}
