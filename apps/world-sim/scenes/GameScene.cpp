// Game Scene - Main gameplay with chunk-based world rendering

#include "../components/GameOverlay.h"
#include "SceneTypes.h"

#include <GL/glew.h>

#include <graphics/Rect.h>
#include <input/InputManager.h>
#include <primitives/Primitives.h>
#include <scene/Scene.h>
#include <scene/SceneManager.h>
#include <utils/Log.h>
#include <world/camera/WorldCamera.h>
#include <world/chunk/ChunkManager.h>
#include <world/chunk/MockWorldSampler.h>
#include <world/rendering/ChunkRenderer.h>

#include <memory>
#include <sstream>

namespace {

constexpr const char* kSceneName = "game";
constexpr uint64_t kDefaultWorldSeed = 12345;
constexpr float kPixelsPerMeter = 8.0F;

class GameScene : public engine::IScene {
  public:
	void onEnter() override {
		LOG_INFO(Game, "GameScene - Entering");

		// Create world systems
		auto sampler = std::make_unique<engine::world::MockWorldSampler>(kDefaultWorldSeed);
		m_chunkManager = std::make_unique<engine::world::ChunkManager>(std::move(sampler));

		m_camera = std::make_unique<engine::world::WorldCamera>();
		m_camera->setPanSpeed(200.0F);

		m_renderer = std::make_unique<engine::world::ChunkRenderer>(kPixelsPerMeter);
		m_renderer->setTileResolution(1);  // Render every tile (no skipping)

		// Initial chunk load
		m_chunkManager->update(m_camera->position());

		// Create overlay with zoom callbacks
		m_overlay = std::make_unique<world_sim::GameOverlay>(world_sim::GameOverlay::Args{
			.onZoomIn = [this]() { m_camera->zoomIn(); },
			.onZoomOut = [this]() { m_camera->zoomOut(); }
		});

		// Initial layout pass
		int viewportW = 0;
		int viewportH = 0;
		Renderer::Primitives::getViewport(viewportW, viewportH);
		m_overlay->layout(Foundation::Rect{0, 0, static_cast<float>(viewportW), static_cast<float>(viewportH)});

		LOG_INFO(Game, "World initialized with seed %llu", kDefaultWorldSeed);
	}

	void handleInput(float dt) override {
		auto& input = engine::InputManager::Get();

		if (input.isKeyPressed(engine::Key::Escape)) {
			sceneManager->switchTo(world_sim::toKey(world_sim::SceneType::MainMenu));
			return;
		}

		// Camera movement
		float dx = 0.0F;
		float dy = 0.0F;

		if (input.isKeyDown(engine::Key::W) || input.isKeyDown(engine::Key::Up)) {
			dy -= 1.0F;
		}
		if (input.isKeyDown(engine::Key::S) || input.isKeyDown(engine::Key::Down)) {
			dy += 1.0F;
		}
		if (input.isKeyDown(engine::Key::A) || input.isKeyDown(engine::Key::Left)) {
			dx -= 1.0F;
		}
		if (input.isKeyDown(engine::Key::D) || input.isKeyDown(engine::Key::Right)) {
			dx += 1.0F;
		}

		if (dx != 0.0F && dy != 0.0F) {
			constexpr float kDiag = 0.7071F;
			dx *= kDiag;
			dy *= kDiag;
		}

		m_camera->move(dx, dy, dt);

		// Zoom with scroll wheel (snaps to discrete levels)
		float scrollDelta = input.consumeScrollDelta();
		if (scrollDelta > 0.0F) {
			m_camera->zoomIn();
		} else if (scrollDelta < 0.0F) {
			m_camera->zoomOut();
		}

		// Handle overlay input (zoom buttons)
		m_overlay->handleInput();
	}

	void update(float dt) override {
		m_camera->update(dt);
		m_chunkManager->update(m_camera->position());
		m_overlay->update(*m_camera, *m_chunkManager);
	}

	void render() override {
		glClearColor(0.05F, 0.08F, 0.12F, 1.0F);
		glClear(GL_COLOR_BUFFER_BIT);

		int w = 0;
		int h = 0;
		Renderer::Primitives::getViewport(w, h);
		m_renderer->render(*m_chunkManager, *m_camera, w, h);

		m_overlay->render();
	}

	void onExit() override {
		LOG_INFO(Game, "GameScene - Exiting");
		m_overlay.reset();
		m_chunkManager.reset();
		m_camera.reset();
		m_renderer.reset();
	}

	std::string exportState() override {
		std::ostringstream oss;
		oss << R"({"scene":"game","chunks":)" << m_chunkManager->loadedChunkCount() << "}";
		return oss.str();
	}

	const char* getName() const override { return kSceneName; }

  private:
	std::unique_ptr<engine::world::ChunkManager> m_chunkManager;
	std::unique_ptr<engine::world::WorldCamera> m_camera;
	std::unique_ptr<engine::world::ChunkRenderer> m_renderer;
	std::unique_ptr<world_sim::GameOverlay> m_overlay;
};

}  // namespace

namespace world_sim::scenes {
extern const world_sim::SceneInfo Game = {kSceneName, []() { return std::make_unique<GameScene>(); }};
}
