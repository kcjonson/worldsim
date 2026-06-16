// World Creator Scene
// Three states: Configuring (parameter panel), Generating (progress only -- the
// globe is hidden so the half-built sphere never shows), Reviewing (final globe;
// click a land tile to inspect it, then Land to drop the colony there).

#include "GameStartConfig.h"
#include "SceneTypes.h"
#include "WorldCreatorModel.h"
#include "scenes/landing/LandingSiteDetailsModel.h"
#include "scenes/landing/LandingSiteDetailsPanel.h"
#include "scenes/shared/GlobeView.h"
#include "ui/ParameterPanel.h"

#include <GL/glew.h>

#include <worldgen/data/WorldData.h>
#include <worldgen/io/PlanetIO.h>
#include <worldgen/sampling/LandingSite.h>

#include <components/progress/ProgressBar.h>
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
#include <vector>

namespace {

constexpr const char* kSceneName = "world_creator";
constexpr float kPanelWidth = 320.0F;
constexpr float kProgressBarHeight = 16.0F;
// Keyboard orbit speed (radians/sec) for arrow keys and WASD.
constexpr float kKeyPanRate = 1.2F;
// Creator opens at preview resolution for fast iteration; must match the
// resolution select's initial value in ParameterPanel (PlanetParams defaults
// to 1024, which the panel would otherwise misreport).
constexpr uint32_t kInitialSubdivision = 256;

class WorldCreatorScene : public engine::IScene {
  public:
	const char* getName() const override { return kSceneName; }
	std::string exportState() override {
		return std::format(
			R"({{"scene":"world_creator","state":{},"globe":{},"site":{}}})",
			static_cast<int>(model.getState()),
			globe.isReady() ? "true" : "false",
			siteValid ? "true" : "false");
	}

	void onEnter() override {
		LOG_INFO(Game, "WorldCreatorScene - Entering");

		int vpW = 0;
		int vpH = 0;
		Renderer::Primitives::getLogicalViewport(vpW, vpH);
		viewportW = static_cast<float>(vpW);
		viewportH = static_cast<float>(vpH);

		buildUI();

		// Pre-fill a random seed so the field isn't blank on entry.
		// Uses the same mechanism as the Random button.
		model.randomizeSeed();
		syncPanelFromModel();
	}

	void onExit() override {
		LOG_INFO(Game, "WorldCreatorScene - Exiting");
		panel.reset();
		progressBar.reset();
		stageText.reset();
		landButton.reset();
	}

	bool handleInput(UI::InputEvent& event) override {
		const bool reviewing =
			model.getState() == world_sim::WorldCreatorState::Reviewing;

		// Mid-drag the globe owns the mouse: widgets must not see the moves
		// (hover flicker) and the drag-ending MouseUp belongs to the camera.
		if (reviewing && globe.isDragging() &&
		    globe.handleInput(event, mainRect(), true)) {
			return true;
		}
		if (panel && panel->handleEvent(event)) return true;
		if (reviewing && landButton && landButton->visible &&
		    landButton->handleEvent(event)) {
			return true;
		}

		// Outside Reviewing the globe doesn't exist on screen: nothing to orbit
		// or pick, and the only generation-time control is Cancel (on the panel).
		if (!reviewing) return false;

		// A press on the location pane belongs to the overlay, not the globe
		// behind it: swallow it so it can't pick a tile or start an orbit.
		if (!globe.isDragging() && detailsPaneShown &&
		    event.type == UI::InputEvent::Type::MouseDown &&
		    detailsPaneBounds.contains(event.position)) {
			return true;
		}

		// Pick before orbit-drag so a left click selects the tile under the
		// cursor; the same click then begins an orbit drag.
		if (event.type == UI::InputEvent::Type::MouseDown &&
		    event.button == engine::MouseButton::Left) {
			if (auto picked = globe.pick(event.position, mainRect())) {
				trySelectSite(*picked);
			}
		}
		return globe.handleInput(event, mainRect(), true);
	}

	void update(float dt) override {
		// ESC -> main menu from any state
		if (engine::InputManager::Get().isKeyPressed(engine::Key::Escape)) {
			if (model.getState() == world_sim::WorldCreatorState::Generating) {
				model.cancelGeneration();
			}
			LOG_INFO(Game, "WorldCreatorScene: returning to main menu");
			sceneManager->switchTo(world_sim::toKey(world_sim::SceneType::MainMenu));
			return;
		}

		globe.update(dt);

		auto state = model.getState();

		if (state == world_sim::WorldCreatorState::Generating) {
			auto prog = model.pollProgress();
			if (progressBar) {
				progressBar->setValue(prog.totalFraction);
			}
			if (stageText) {
				stageText->text = prog.stageName ? prog.stageName : "";
			}

			// Re-check after poll (state may have changed)
			auto newState = model.getState();
			if (newState != state) {
				// Anything other than a clean completion or a user cancel is an error
				if (newState == world_sim::WorldCreatorState::Configuring &&
				    prog.state != worldgen::GenerationProgress::State::Cancelled) {
					errorText = "World generation failed. Adjust parameters and try again.";
					LOG_ERROR(Game, "WorldCreatorScene: generation ended without a result (state=%d)",
					          static_cast<int>(prog.state));
				}
				onStateChanged(newState);
			}
		} else if (state == world_sim::WorldCreatorState::Reviewing) {
			handleCameraKeys(dt);
		}

		if (panel) { panel->update(dt); }
		if (landButton) { landButton->update(dt); }
	}

	void render() override {
		glClearColor(0.08F, 0.07F, 0.12F, 1.0F);
		glClear(GL_COLOR_BUFFER_BIT);

		// The pane republishes its bounds below only on frames it actually
		// draws; clear first so input never trusts a stale rect.
		detailsPaneShown = false;

		Foundation::Rect rect = mainRect();
		auto state = model.getState();

		// The globe exists on screen only once generation completes; while
		// configuring or generating we show a placeholder, never the sphere.
		const bool showGlobe =
			state == world_sim::WorldCreatorState::Reviewing && globe.isReady();

		// 3D pass first: Primitives batches flush after the scene, so all 2D
		// UI composites on top of the blitted globe.
		if (showGlobe) {
			globe.render(rect, viewportW, viewportH);
		} else {
			renderPlaceholder(rect);
		}

		renderTitle();

		if (panel) { panel->render(); }

		if (state == world_sim::WorldCreatorState::Generating) {
			if (stageText)   { stageText->render(); }
			if (progressBar) { progressBar->render(); }
		} else if (state == world_sim::WorldCreatorState::Reviewing) {
			renderReviewingOverlay(rect);
		} else if (!errorText.empty()) {
			UI::Text err(UI::Text::Args{
				.position = {rect.x, viewportH - 60.0F},
				.text = errorText,
				.style = {
					.color = Foundation::Color{0.9F, 0.4F, 0.4F, 1.0F},
					.fontSize = 14.0F,
					.hAlign = Foundation::HorizontalAlign::Left,
					.vAlign = Foundation::VerticalAlign::Middle,
				},
			});
			err.render();
		}

		if (showGlobe) {
			renderModeHint(rect);
		}

		renderEscHint();
	}

  private:
	world_sim::WorldCreatorModel model;

	float viewportW{1280.0F};
	float viewportH{720.0F};

	std::unique_ptr<world_sim::ParameterPanel> panel;
	std::unique_ptr<UI::ProgressBar> progressBar;
	std::unique_ptr<UI::Text>        stageText;
	std::unique_ptr<UI::Button>      landButton;

	world_sim::GlobeView globe;
	std::string errorText;

	// Landing selection (Reviewing only). The site auto-suggests on completion
	// and updates on each land-tile click.
	planetview::LatLon                 selectedSite{};
	bool                               siteValid{false};
	std::string                        pickHint;
	world_sim::LandingSiteDetails      details;
	world_sim::LandingSiteDetailsPanel detailsPanel;

	// On-screen bounds of the location pane, republished each frame it draws so
	// input can swallow clicks over the overlay rather than picking/orbiting the
	// globe behind it. Cleared at the top of every render().
	Foundation::Rect detailsPaneBounds{};
	bool             detailsPaneShown{false};

	// Main content area right of the parameter panel. Bottom strip (80px)
	// holds the progress bar / stage text / stats.
	Foundation::Rect mainRect() const {
		float x = kPanelWidth + 20.0F;
		return {x, 40.0F, viewportW - x - 20.0F, viewportH - 120.0F};
	}

	void buildUI() {
		world_sim::ParameterPanelCallbacks cbs;

		cbs.onPresetChanged = [this](const std::string& val) {
			worldgen::Preset preset = worldgen::Preset::EarthLike;
			if (val == "desert_world")    preset = worldgen::Preset::DesertWorld;
			else if (val == "ocean_world")    preset = worldgen::Preset::OceanWorld;
			else if (val == "frozen_world")   preset = worldgen::Preset::FrozenWorld;
			else if (val == "volcanic_world") preset = worldgen::Preset::VolcanicWorld;
			else if (val == "ancient_garden") preset = worldgen::Preset::AncientGarden;
			model.setPreset(preset);
			syncPanelFromModel();
		};
		cbs.onWaterAmount    = [this](double v) { model.setWaterAmount(v); };
		cbs.onTectonicPlates = [this](double v) { model.setTectonicPlates(static_cast<int>(v)); };
		cbs.onPlanetRadius   = [this](double v) { model.setPlanetRadius(v); };
		cbs.onRotationRate   = [this](double v) { model.setRotationRate(v); };
		cbs.onPlanetAge      = [this](double v) { model.setPlanetAge(v); };
		cbs.onAtmosphere     = [this](double v) { model.setAtmosphereStrength(v); };
		cbs.onStarTemperature = [this](double v) { model.setStarTemperature(v); };
		cbs.onSemiMajorAxis  = [this](double v) { model.setSemiMajorAxis(v); };
		cbs.onEccentricity   = [this](double v) { model.setEccentricity(v); };
		cbs.onResolutionChanged = [this](const std::string& val) {
			try {
				model.setGridSubdivision(static_cast<uint32_t>(std::stoul(val)));
			} catch (...) {}
		};
		cbs.onSeedChanged = [this](const std::string& s) {
			try {
				model.setSeed(std::stoull(s));
			} catch (...) {}
		};
		cbs.onRandomize = [this]() {
			model.randomizeSeed();
			syncPanelFromModel();
		};
		cbs.onGenerate = [this]() { startGeneration(); };
		cbs.onCancel   = [this]() {
			model.cancelGeneration();
			if (panel) panel->setGenerating(false);
		};

		panel = std::make_unique<world_sim::ParameterPanel>(
			Foundation::Vec2{0.0F, 30.0F}, std::move(cbs));
		model.setGridSubdivision(kInitialSubdivision);

		// Progress bar (hidden until Generating)
		float mainX = kPanelWidth + 20.0F;
		float barW  = viewportW - mainX - 20.0F;
		progressBar = std::make_unique<UI::ProgressBar>(UI::ProgressBar::Args{
			.position = {mainX, viewportH - 50.0F},
			.size = {barW, kProgressBarHeight},
			.value = 0.0F,
			.label = "",
		});
		progressBar->visible = false;

		stageText = std::make_unique<UI::Text>(UI::Text::Args{
			.position = {mainX, viewportH - 72.0F},
			.text = "",
			.style = {
				.color = Foundation::Color{0.7F, 0.7F, 0.7F, 1.0F},
				.fontSize = 14.0F,
			},
		});
		stageText->visible = false;

		// "Land" lives in the location pane and is repositioned under it each
		// frame; it commits the selected site and starts the game there.
		landButton = std::make_unique<UI::Button>(UI::Button::Args{
			.label = "Land here",
			.position = {mainX, viewportH - 50.0F},
			.size = {world_sim::LandingSiteDetailsPanel::kWidth, 34.0F},
			.type = UI::Button::Type::Primary,
			.onClick = [this]() { land(); },
			.id = "btn_land",
		});
		landButton->visible = false;
	}

	void syncPanelFromModel() {
		const auto& p = model.getParams();
		if (panel) {
			panel->syncValues(
				p.waterAmount * 100.0,  // fraction -> percent for UI
				p.tectonicPlateCount,
				p.planetRadius,
				p.rotationRate,
				p.planetAge,
				p.atmosphereStrength,
				p.starTemperature,
				p.semiMajorAxis,
				p.eccentricity,
				p.seed);
		}
	}

	void startGeneration() {
		if (panel && !panel->seedIsValid()) {
			return;
		}

		// Blank seed means "surprise me": pick a fresh seed each run so regenerating
		// doesn't silently reproduce the identical world from the preset default.
		if (panel && panel->seedIsEmpty()) {
			model.randomizeSeed();
			syncPanelFromModel();
		}

		errorText.clear();
		siteValid = false;
		pickHint.clear();

		model.startGeneration();
		if (panel) panel->setGenerating(true);
		if (progressBar) { progressBar->setValue(0.0F); progressBar->visible = true; }
		if (stageText)   { stageText->text = "Starting..."; stageText->visible = true; }
		if (landButton) landButton->visible = false;
	}

	void onStateChanged(world_sim::WorldCreatorState newState) {
		if (newState == world_sim::WorldCreatorState::Reviewing) {
			if (panel)       panel->setGenerating(false);
			if (progressBar) progressBar->visible = false;
			if (stageText)   stageText->visible = false;

			if (auto result = model.getResult()) {
				globe.setWorld(result);
				// Suggest a habitable starting point so the pane has content the
				// moment review begins; the player can click to choose another.
				auto suggested = worldgen::findDefaultLandingSite(*result);
				selectedSite = {static_cast<float>(suggested.latDeg),
				                static_cast<float>(suggested.lonDeg)};
				siteValid = true;
				pickHint.clear();
				refreshDetails();
				if (landButton) landButton->visible = true;
			}
		} else if (newState == world_sim::WorldCreatorState::Configuring) {
			if (panel)       panel->setGenerating(false);
			if (progressBar) progressBar->visible = false;
			if (stageText)   stageText->visible = false;
			if (landButton)  landButton->visible = false;
			siteValid = false;
		}
	}

	void handleCameraKeys(float dt) {
		// While the seed field is focused, keys belong to it (text editing,
		// cursor arrows), not the camera.
		if (panel && panel->isSeedFocused()) return;

		auto& im = engine::InputManager::Get();
		const float step = kKeyPanRate * dt;
		float dYaw = 0.0F;
		float dPitch = 0.0F;
		if (im.isKeyDown(engine::Key::Left)  || im.isKeyDown(engine::Key::A)) dYaw   += step;
		if (im.isKeyDown(engine::Key::Right) || im.isKeyDown(engine::Key::D)) dYaw   -= step;
		if (im.isKeyDown(engine::Key::Up)    || im.isKeyDown(engine::Key::W)) dPitch += step;
		if (im.isKeyDown(engine::Key::Down)  || im.isKeyDown(engine::Key::S)) dPitch -= step;
		if (dYaw != 0.0F || dPitch != 0.0F) globe.panCamera(dYaw, dPitch);
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

	void trySelectSite(planetview::LatLon picked) {
		auto result = model.getResult();
		if (!result || !result->grid) return;

		glm::vec3 unit = planetview::latLonToUnitSphere(picked.latDeg, picked.lonDeg);
		worldgen::TileId tile = result->grid->fromUnitVector(
			worldgen::Vec3d{unit.x, unit.y, unit.z});

		if (isWaterTile(*result, tile)) {
			pickHint = "Select a land tile to start the colony";
			return;
		}

		selectedSite = picked;
		siteValid = true;
		pickHint.clear();
		refreshDetails();
		LOG_INFO(Game, "WorldCreatorScene: selected lat=%.2f lon=%.2f tile=%u",
		         static_cast<double>(picked.latDeg),
		         static_cast<double>(picked.lonDeg),
		         tile);
	}

	void refreshDetails() {
		auto result = model.getResult();
		if (siteValid && result) {
			details = world_sim::buildLandingSiteDetails(
				*result, selectedSite.latDeg, selectedSite.lonDeg);
		}
	}

	void land() {
		auto result = model.getResult();
		if (!siteValid || !result) return;

		LOG_INFO(Game, "WorldCreatorScene: landing lat=%.2f lon=%.2f",
		         static_cast<double>(selectedSite.latDeg),
		         static_cast<double>(selectedSite.lonDeg));

		// Persist the accepted world: reloadable without regeneration, stable
		// artifact for sharing and determinism checks. Best-effort; starting
		// the game must not be blocked by a failed write.
		std::string planetPath =
			std::format("planets/planet-{}.wsplanet", result->params.seed);
		if (worldgen::savePlanet(*result, planetPath)) {
			LOG_INFO(Game, "WorldCreatorScene: saved planet to %s", planetPath.c_str());
		} else {
			LOG_ERROR(Game, "WorldCreatorScene: failed to save planet to %s", planetPath.c_str());
		}

		auto config = std::make_unique<world_sim::GameStartConfig>();
		config->source = world_sim::GameStartConfig::Source::NewGame;
		config->world = result;
		config->landingLatDeg = selectedSite.latDeg;
		config->landingLonDeg = selectedSite.lonDeg;
		world_sim::GameStartConfig::SetPending(std::move(config));
		sceneManager->switchTo(world_sim::toKey(world_sim::SceneType::GameLoading));
	}

	void renderPlaceholder(const Foundation::Rect& rect) {
		Renderer::Primitives::drawRect({
			.bounds = rect,
			.style = {
				.fill = Foundation::Color{0.05F, 0.05F, 0.08F, 1.0F},
				.border = Foundation::BorderStyle{
					.color = Foundation::Color{0.2F, 0.2F, 0.25F, 1.0F},
					.width = 1.0F,
				},
			},
			.id = "planet_view_placeholder",
		});

		bool generating = model.getState() == world_sim::WorldCreatorState::Generating;
		UI::Text placeholder(UI::Text::Args{
			.position = {rect.x + rect.width * 0.5F, rect.y + rect.height * 0.5F},
			.text = generating ? "Generating World..." : "Set parameters and press Generate",
			.style = {
				.color = generating
					? Foundation::Color{0.7F, 0.75F, 1.0F, 1.0F}
					: Foundation::Color{0.3F, 0.3F, 0.35F, 1.0F},
				.fontSize = 18.0F,
				.hAlign = Foundation::HorizontalAlign::Center,
				.vAlign = Foundation::VerticalAlign::Middle,
			},
		});
		placeholder.render();
	}

	void renderTitle() {
		UI::Text title(UI::Text::Args{
			.position = {kPanelWidth + (viewportW - kPanelWidth) * 0.5F, 14.0F},
			.text = "World Creator",
			.style = {
				.color = Foundation::Color::white(),
				.fontSize = 20.0F,
				.hAlign = Foundation::HorizontalAlign::Center,
				.vAlign = Foundation::VerticalAlign::Middle,
			},
		});
		title.render();
	}

	void renderEscHint() {
		UI::Text hint(UI::Text::Args{
			.position = {viewportW - 12.0F, 14.0F},
			.text = "ESC: Back to menu",
			.style = {
				.color = Foundation::Color{0.45F, 0.45F, 0.45F, 1.0F},
				.fontSize = 13.0F,
				.hAlign = Foundation::HorizontalAlign::Right,
				.vAlign = Foundation::VerticalAlign::Middle,
			},
		});
		hint.render();
	}

	void renderModeHint(const Foundation::Rect& rect) {
		std::string label = std::string("Mode: ") +
			planetview::colorModeName(globe.colorMode()) +
			"   (click a land tile to inspect, drag or arrow/WASD keys to orbit, scroll to zoom, right-click to cycle)";
		UI::Text hint(UI::Text::Args{
			.position = {rect.x + 10.0F, rect.y + 10.0F},
			.text = label,
			.style = {
				.color = Foundation::Color{0.8F, 0.8F, 0.8F, 0.9F},
				.fontSize = 12.0F,
				.hAlign = Foundation::HorizontalAlign::Left,
				.vAlign = Foundation::VerticalAlign::Top,
			},
		});
		hint.render();
	}

	void renderReviewingOverlay(const Foundation::Rect& rect) {
		// Marker on the selected site.
		if (siteValid) {
			float sx = 0.0F;
			float sy = 0.0F;
			if (globe.projectLatLon(selectedSite, rect, sx, sy)) {
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

		// Stats line under the globe. Only report stats whose underlying fields
		// the pipeline actually produced; unimplemented stages must not
		// masquerade as data.
		auto worldResult = model.getResult();
		std::string statsStr;
		if (worldResult) {
			const auto& summary = worldResult->summary;
			std::vector<std::string> parts;
			if (hasField(*worldResult, worldgen::WorldField::Elevation)) {
				parts.push_back(std::format("Land: {:.0f}%", summary.landFraction * 100.0F));
			}
			if (hasField(*worldResult, worldgen::WorldField::TemperatureMean)) {
				parts.push_back(std::format("Mean Temp: {:.1f}C", summary.meanTemperatureC));
			}
			if (hasField(*worldResult, worldgen::WorldField::Biome)) {
				parts.push_back(std::format("Habitability: {:.0f}%", summary.habitability * 100.0F));
			}
			for (size_t i = 0; i < parts.size(); ++i) {
				if (i > 0) statsStr += "  |  ";
				statsStr += parts[i];
			}
			if (statsStr.empty()) {
				statsStr = "World generated (no summary stats yet)";
			}
		}

		if (!statsStr.empty()) {
			UI::Text stats(UI::Text::Args{
				.position = {rect.x + rect.width * 0.5F, rect.y + rect.height + 20.0F},
				.text = statsStr,
				.style = {
					.color = Foundation::Color{0.75F, 0.75F, 0.75F, 1.0F},
					.fontSize = 15.0F,
					.hAlign = Foundation::HorizontalAlign::Center,
					.vAlign = Foundation::VerticalAlign::Middle,
				},
			});
			stats.render();
		}

		// Location pane (top-right of the globe area) with the Land button
		// docked beneath it. Publish the pane bounds for input click-swallowing.
		if (siteValid) {
			detailsPaneBounds =
				detailsPanel.render(details, rect.right() - 12.0F, rect.y + 12.0F);
			detailsPaneShown = true;
			if (landButton) {
				landButton->visible = true;
				landButton->setPosition(detailsPaneBounds.x,
				                        detailsPaneBounds.y + detailsPaneBounds.height + 6.0F);
				landButton->render();
			}
		}

		// Pick errors (e.g. clicking water) surface as a short hint at the
		// bottom; selecting a valid tile clears it.
		if (!pickHint.empty()) {
			UI::Text text(UI::Text::Args{
				.position = {rect.x + rect.width * 0.5F, viewportH - 38.0F},
				.text = pickHint,
				.style = {
					.color = Foundation::Color{0.95F, 0.75F, 0.3F, 1.0F},
					.fontSize = 14.0F,
					.hAlign = Foundation::HorizontalAlign::Center,
					.vAlign = Foundation::VerticalAlign::Middle,
				},
			});
			text.render();
		}
	}
};

} // namespace

namespace world_sim::scenes {
	extern const world_sim::SceneInfo WorldCreator = {kSceneName, []() {
		return std::make_unique<WorldCreatorScene>();
	}};
}
