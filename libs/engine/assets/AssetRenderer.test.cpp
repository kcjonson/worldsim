// Tests for AssetRenderer: the shared offscreen render path.
// These need a real GL context; they skip gracefully on a headless box.

#include "assets/AssetRenderer.h"
#include "assets/AssetRegistry.h"

#include "primitives/Primitives.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

using namespace engine::assets;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

	// <root>/libs/engine/assets/AssetRenderer.test.cpp -> walk up to <root>.
	std::filesystem::path projectRoot() {
		std::filesystem::path p = __FILE__;
		return p
			.parent_path()	// libs/engine/assets
			.parent_path()	// libs/engine
			.parent_path()	// libs
			.parent_path(); // project root
	}

	// Make "shaders/uber.vert" resolvable from the working directory so
	// Primitives::init can load the uber shader. Returns false if not found.
	bool ensureShadersResolvable() {
		namespace fs = std::filesystem;
		const std::vector<fs::path> candidates = {
			fs::current_path(),
			projectRoot() / "build",
			projectRoot() / "libs" / "renderer",
		};
		for (const auto& c : candidates) {
			std::error_code ec;
			if (fs::exists(c / "shaders" / "uber.vert", ec)) {
				fs::current_path(c, ec);
				return !ec;
			}
		}
		return false;
	}

	// Brings up a hidden GL context + the renderer primitives for the duration of
	// a test. `ok` is false when no context/shaders are available (headless CI).
	struct GlFixture {
		GLFWwindow*			  window = nullptr;
		bool				  ok = false;
		std::filesystem::path originalCwd;

		GlFixture() {
			// Captured before ensureShadersResolvable() may change cwd; restored in the dtor.
			std::error_code cwdEc;
			originalCwd = std::filesystem::current_path(cwdEc);

			if (glfwInit() != GLFW_TRUE) {
				return;
			}
			glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
			glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
			glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
			glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
			glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
			window = glfwCreateWindow(64, 64, "asset-render-test", nullptr, nullptr);
			if (window == nullptr) {
				glfwTerminate();
				return;
			}
			glfwMakeContextCurrent(window);
			glewExperimental = GL_TRUE;
			if (glewInit() != GLEW_OK) {
				glfwDestroyWindow(window);
				glfwTerminate();
				return;
			}
			if (!ensureShadersResolvable()) {
				glfwDestroyWindow(window);
				glfwTerminate();
				return;
			}
			Renderer::Primitives::init(nullptr);
			ok = true;
		}

		~GlFixture() {
			if (ok) {
				Renderer::Primitives::shutdown();
			}
			if (window != nullptr) {
				glfwDestroyWindow(window);
				glfwTerminate();
			}
			// ensureShadersResolvable() may have changed the working directory;
			// restore it so test execution order can't leak between tests.
			if (!originalCwd.empty()) {
				std::error_code ec;
				std::filesystem::current_path(originalCwd, ec);
			}
		}

		GlFixture(const GlFixture&) = delete;
		GlFixture& operator=(const GlFixture&) = delete;
	};

	void loadAssets() {
		AssetRegistry::Get().clear();
		AssetRegistry::Get().setSharedScriptsPath(projectRoot() / "assets" / "shared" / "scripts");
		AssetRegistry::Get().loadDefinitionsFromFolder((projectRoot() / "assets" / "world").string());
	}

	std::string firstOfType(AssetType type) {
		for (const auto& name : AssetRegistry::Get().getDefinitionNames()) {
			const AssetDefinition* d = AssetRegistry::Get().getDefinition(name);
			if (d != nullptr && d->assetType == type) {
				return name;
			}
		}
		return {};
	}

} // namespace

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST(AssetRendererTest, ProceduralRenderIsDeterministic) {
	GlFixture gl;
	if (!gl.ok) {
		GTEST_SKIP() << "No GL context / shaders unavailable (headless CI)";
	}
	loadAssets();

	const std::string proc = firstOfType(AssetType::Procedural);
	if (proc.empty()) {
		GTEST_SKIP() << "No procedural asset in the library";
	}

	const Foundation::Color bg(0.0F, 0.0F, 0.0F, 1.0F);
	const auto				a = renderToPixels(proc, 96, 96, bg, 7);
	const auto				b = renderToPixels(proc, 96, 96, bg, 7);

	ASSERT_FALSE(a.empty());
	EXPECT_EQ(a, b) << "same seed must produce identical pixels (" << proc << ")";

	// At least one procedural generator must vary with the seed.
	bool anyVaries = false;
	for (const auto& name : AssetRegistry::Get().getDefinitionNames()) {
		const AssetDefinition* d = AssetRegistry::Get().getDefinition(name);
		if (d == nullptr || d->assetType != AssetType::Procedural) {
			continue;
		}
		const auto s7 = renderToPixels(name, 64, 64, bg, 7);
		const auto s8 = renderToPixels(name, 64, 64, bg, 8);
		if (!s7.empty() && s7 != s8) {
			anyVaries = true;
			break;
		}
	}
	EXPECT_TRUE(anyVaries) << "expected a procedural generator to vary with the seed";
}

TEST(AssetRendererTest, RendersVisibleGeometry) {
	GlFixture gl;
	if (!gl.ok) {
		GTEST_SKIP() << "No GL context / shaders unavailable (headless CI)";
	}
	loadAssets();

	const std::string simple = firstOfType(AssetType::Simple);
	if (simple.empty()) {
		GTEST_SKIP() << "No simple asset in the library";
	}

	// Magenta background so drawn geometry is distinguishable from it.
	const Foundation::Color bg(1.0F, 0.0F, 1.0F, 1.0F);
	const auto				px = renderToPixels(simple, 128, 128, bg, 0);
	ASSERT_FALSE(px.empty());

	int nonBackground = 0;
	for (size_t i = 0; i + 3 < px.size(); i += 4) {
		const bool isBg = px[i] == 255 && px[i + 1] == 0 && px[i + 2] == 255;
		if (!isBg) {
			++nonBackground;
		}
	}
	EXPECT_GT(nonBackground, 100) << "expected the asset (" << simple << ") to draw visible geometry";
}
