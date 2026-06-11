// Landing Site Scene
// Shows the generated planet; the player clicks a land tile to choose where
// the colony starts, then confirms to enter the 2D gameplay world there.

#include "GameStartConfig.h"
#include "SceneTypes.h"
#include "scenes/shared/GlobeView.h"

#include <GL/glew.h>

#include <worldgen/data/Biome.h>
#include <worldgen/data/WorldData.h>
#include <worldgen/io/PlanetIO.h>
#include <worldgen/sampling/LandingSite.h>

#include <components/button/Button.h>
#include <graphics/Color.h>
#include <input/InputEvent.h>
#include <input/InputManager.h>
#include <primitives/Primitives.h>
#include <scene/Scene.h>
#include <scene/SceneManager.h>
#include <shapes/Shapes.h>
#include <utils/Log.h>

#include <format>
#include <memory>
#include <string>

namespace {

constexpr const char* kSceneName = "landing_site";

class LandingSiteScene : public engine::IScene {
  public:
	const char* getName() const override { return kSceneName; }

	std::string exportState() override {
		return std::format(
			R"({{"scene":"landing_site","globe":{},"lat":{:.3f},"lon":{:.3f},"valid":{}}})",
			globe.isReady() ? "true" : "false",
			site.latDeg, site.lonDeg,
			siteValid ? "true" : "false");
	}

	void onEnter() override {
		LOG_INFO(Game, "LandingSiteScene - Entering");

		int vpW = 0;
		int vpH = 0;
		Renderer::Primitives::getLogicalViewport(vpW, vpH);
		viewportW = static_cast<float>(vpW);
		viewportH = static_cast<float>(vpH);

		config = world_sim::GameStartConfig::Take();
		if (!config || !config->world) {
			LOG_ERROR(Game, "LandingSiteScene: no world to land on, returning to creator");
			missingWorld = true;
			return;
		}

		globe.setWorld(config->world);

		auto suggested = worldgen::findDefaultLandingSite(*config->world);
		site = {static_cast<float>(suggested.latDeg), static_cast<float>(suggested.lonDeg)};
		siteValid = true;

		buildUI();
	}

	void onExit() override {
		LOG_INFO(Game, "LandingSiteScene - Exiting");
		confirmButton.reset();
		backButton.reset();
	}

	bool handleInput(UI::InputEvent& event) override {
		if (missingWorld) return false;

		if (confirmButton && confirmButton->handleEvent(event)) return true;
		if (backButton && backButton->handleEvent(event)) return true;

		// Pick before orbit-drag so a click selects the tile under the cursor
		if (event.type == UI::InputEvent::Type::MouseDown &&
		    event.button == engine::MouseButton::Left) {
			if (auto picked = globe.pick(event.position, mainRect())) {
				trySelectSite(*picked);
			}
		}

		return globe.handleInput(event, mainRect(), false);
	}

	void update(float dt) override {
		if (missingWorld) {
			sceneManager->switchTo(world_sim::toKey(world_sim::SceneType::WorldCreator));
			return;
		}

		if (engine::InputManager::Get().isKeyPressed(engine::Key::Escape)) {
			goBack();
			return;
		}

		globe.update(dt);
		if (confirmButton) confirmButton->update(dt);
		if (backButton) backButton->update(dt);
	}

	void render() override {
		glClearColor(0.05F, 0.05F, 0.09F, 1.0F);
		glClear(GL_COLOR_BUFFER_BIT);

		if (missingWorld) return;

		Foundation::Rect rect = mainRect();
		globe.render(rect, viewportW, viewportH);

		// Marker on the selected site
		if (siteValid) {
			float sx = 0.0F;
			float sy = 0.0F;
			if (globe.projectLatLon(site, rect, sx, sy)) {
				Renderer::Primitives::drawCircle({
					.center = {sx, sy},
					.radius = 7.0F,
					.style = {
						.fill = Foundation::Color{1.0F, 0.85F, 0.1F, 0.9F},
						.border = Foundation::BorderStyle{
							.color = Foundation::Color{0.1F, 0.1F, 0.1F, 1.0F},
							.width = 2.0F,
						},
					},
				});
			}
		}

		renderHeader();
		renderSiteInfo(rect);

		if (confirmButton) confirmButton->render();
		if (backButton) backButton->render();
	}

  private:
	float viewportW{1280.0F};
	float viewportH{720.0F};

	std::unique_ptr<world_sim::GameStartConfig> config;
	bool missingWorld{false};

	world_sim::GlobeView globe;
	planetview::LatLon   site;
	bool                 siteValid{false};
	std::string          pickHint;

	std::unique_ptr<UI::Button> confirmButton;
	std::unique_ptr<UI::Button> backButton;

	Foundation::Rect mainRect() const {
		return {20.0F, 40.0F, viewportW - 40.0F, viewportH - 130.0F};
	}

	void buildUI() {
		backButton = std::make_unique<UI::Button>(UI::Button::Args{
			.label = "Back",
			.position = {20.0F, viewportH - 56.0F},
			.size = {120.0F, 36.0F},
			.type = UI::Button::Type::Secondary,
			.onClick = [this]() { goBack(); },
			.id = "btn_landing_back",
		});

		confirmButton = std::make_unique<UI::Button>(UI::Button::Args{
			.label = "Confirm Landing Site",
			.position = {viewportW - 240.0F, viewportH - 56.0F},
			.size = {220.0F, 36.0F},
			.type = UI::Button::Type::Primary,
			.onClick = [this]() { confirm(); },
			.id = "btn_landing_confirm",
		});
	}

	void trySelectSite(planetview::LatLon picked) {
		const auto& world = *config->world;
		if (!world.grid) return;

		glm::vec3 unit = planetview::latLonToUnitSphere(picked.latDeg, picked.lonDeg);
		worldgen::TileId tile = world.grid->fromUnitVector(
			worldgen::Vec3d{unit.x, unit.y, unit.z});

		if (isWaterTile(world, tile)) {
			pickHint = "Select a land tile to start the colony";
			return;
		}

		site = picked;
		siteValid = true;
		pickHint.clear();
		LOG_INFO(Game, "LandingSiteScene: selected lat=%.2f lon=%.2f tile=%u",
		         static_cast<double>(picked.latDeg),
		         static_cast<double>(picked.lonDeg),
		         tile);
	}

	static bool hasField(const worldgen::GeneratedWorld& world, worldgen::WorldField f) {
		return (world.validFields & static_cast<uint32_t>(f)) != 0;
	}

	static bool isWaterTile(const worldgen::GeneratedWorld& world, worldgen::TileId tile) {
		if (hasField(world, worldgen::WorldField::Flags) && tile < world.data.flags.size()) {
			return (world.data.flags[tile] & (worldgen::kFlagOcean | worldgen::kFlagLake)) != 0;
		}
		if (hasField(world, worldgen::WorldField::Elevation) && tile < world.data.elevation.size()) {
			return world.data.elevation[tile] < world.seaLevelMeters;
		}
		return false;
	}

	void renderHeader() {
		UI::Text title(UI::Text::Args{
			.position = {viewportW * 0.5F, 14.0F},
			.text = "Choose Landing Site",
			.style = {
				.color = Foundation::Color::white(),
				.fontSize = 20.0F,
				.hAlign = Foundation::HorizontalAlign::Center,
				.vAlign = Foundation::VerticalAlign::Middle,
			},
		});
		title.render();

		UI::Text hint(UI::Text::Args{
			.position = {viewportW - 12.0F, 14.0F},
			.text = "Click a land tile  |  ESC: Back",
			.style = {
				.color = Foundation::Color{0.45F, 0.45F, 0.45F, 1.0F},
				.fontSize = 13.0F,
				.hAlign = Foundation::HorizontalAlign::Right,
				.vAlign = Foundation::VerticalAlign::Middle,
			},
		});
		hint.render();
	}

	void renderSiteInfo(const Foundation::Rect& rect) {
		std::string info;
		if (siteValid) {
			info = std::format("Landing site: {:.2f}{}, {:.2f}{}",
				std::abs(site.latDeg), site.latDeg >= 0.0F ? "N" : "S",
				std::abs(site.lonDeg), site.lonDeg >= 0.0F ? "E" : "W");

			const auto& world = *config->world;
			glm::vec3 unit = planetview::latLonToUnitSphere(site.latDeg, site.lonDeg);
			worldgen::TileId tile = world.grid->fromUnitVector(
				worldgen::Vec3d{unit.x, unit.y, unit.z});

			if (hasField(world, worldgen::WorldField::Biome) && tile < world.data.biome.size()) {
				info += std::format("  |  Biome: {}",
					worldgen::biomeToString(static_cast<worldgen::Biome>(world.data.biome[tile])));
			}
			if (hasField(world, worldgen::WorldField::Elevation) && tile < world.data.elevation.size()) {
				info += std::format("  |  Elevation: {:.0f}m",
					world.data.elevation[tile] - world.seaLevelMeters);
			}
		}
		if (!pickHint.empty()) {
			info += (info.empty() ? "" : "    ") + pickHint;
		}

		UI::Text text(UI::Text::Args{
			.position = {rect.x + rect.width * 0.5F, viewportH - 38.0F},
			.text = info,
			.style = {
				.color = pickHint.empty()
					? Foundation::Color{0.8F, 0.8F, 0.8F, 1.0F}
					: Foundation::Color{0.95F, 0.75F, 0.3F, 1.0F},
				.fontSize = 14.0F,
				.hAlign = Foundation::HorizontalAlign::Center,
				.vAlign = Foundation::VerticalAlign::Middle,
			},
		});
		text.render();
	}

	void goBack() {
		LOG_INFO(Game, "LandingSiteScene: back to world creator");
		sceneManager->switchTo(world_sim::toKey(world_sim::SceneType::WorldCreator));
	}

	void confirm() {
		if (!siteValid || !config || !config->world) return;

		LOG_INFO(Game, "LandingSiteScene: confirmed lat=%.2f lon=%.2f",
		         static_cast<double>(site.latDeg),
		         static_cast<double>(site.lonDeg));

		// Persist the accepted world: reloadable without regeneration, stable
		// artifact for sharing and determinism checks. Best-effort; starting
		// the game must not be blocked by a failed write.
		std::string planetPath = std::format("planets/planet-{}.wsplanet", config->world->params.seed);
		if (worldgen::savePlanet(*config->world, planetPath)) {
			LOG_INFO(Game, "LandingSiteScene: saved planet to %s", planetPath.c_str());
		} else {
			LOG_ERROR(Game, "LandingSiteScene: failed to save planet to %s", planetPath.c_str());
		}

		config->landingLatDeg = site.latDeg;
		config->landingLonDeg = site.lonDeg;
		world_sim::GameStartConfig::SetPending(std::move(config));
		sceneManager->switchTo(world_sim::toKey(world_sim::SceneType::GameLoading));
	}
};

} // namespace

namespace world_sim::scenes {
	extern const world_sim::SceneInfo LandingSite = {kSceneName, []() {
		return std::make_unique<LandingSiteScene>();
	}};
}
