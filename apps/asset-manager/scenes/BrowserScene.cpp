#include "AssetGridView.h"
#include "SceneTypes.h"

#include <assets/AssetRegistry.h>
#include <graphics/Color.h>
#include <primitives/Primitives.h>
#include <scene/Scene.h>
#include <shapes/Shapes.h>
#include <utils/Log.h>

#include <GL/glew.h>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr const char* kSceneName = "browser";

class BrowserScene : public engine::IScene {
  public:
	void onEnter() override {
		LOG_INFO(Engine, "AssetManager BrowserScene - Entering");

		std::vector<std::string> names = engine::assets::AssetRegistry::Get().getDefinitionNames();
		std::sort(names.begin(), names.end());
		LOG_INFO(Engine, "AssetManager: %zu definitions", names.size());

		m_grid = std::make_unique<asset_manager::AssetGridView>();
		m_grid->setItems(std::move(names));
		layoutGrid();

		m_title = std::make_unique<UI::Text>(UI::Text::Args{
			.position = {20.0F, 16.0F},
			.text = "Asset Library",
			.style =
				{
					.color = Foundation::Color::white(),
					.fontSize = 28.0F,
				},
			.id = "am_title",
		});
	}

	void update(float /*dt*/) override {}

	void render() override {
		glClearColor(0.10F, 0.10F, 0.13F, 1.0F);
		glClear(GL_COLOR_BUFFER_BIT);
		if (m_title) {
			m_title->render();
		}
		if (m_grid) {
			m_grid->render();
		}
	}

	void onExit() override {
		m_grid.reset();
		m_title.reset();
	}

	std::string exportState() override { return R"({"scene": "browser"})"; }
	const char* getName() const override { return kSceneName; }

  private:
	void layoutGrid() {
		int viewportW = 0;
		int viewportH = 0;
		Renderer::Primitives::getLogicalViewport(viewportW, viewportH);
		constexpr float kTop = 56.0F;
		m_grid->setBounds({20.0F, kTop, static_cast<float>(viewportW) - 40.0F, static_cast<float>(viewportH) - kTop - 20.0F});
	}

	std::unique_ptr<asset_manager::AssetGridView> m_grid;
	std::unique_ptr<UI::Text>					  m_title;
};

} // namespace

namespace asset_manager::scenes {
	extern const asset_manager::SceneInfo Browser = {kSceneName, []() { return std::make_unique<BrowserScene>(); }};
}
