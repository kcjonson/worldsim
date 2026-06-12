// World Creator Scene
// Three states: Configuring (parameter panel), Generating (progress + live globe),
// Reviewing (final globe + stats + continue to landing site selection).

#include "GameStartConfig.h"
#include "SceneTypes.h"
#include "WorldCreatorModel.h"
#include "scenes/shared/GlobeView.h"
#include "ui/ParameterPanel.h"

#include <GL/glew.h>

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
// Creator opens at preview resolution for fast iteration; must match the
// resolution select's initial value in ParameterPanel (PlanetParams defaults
// to 1024, which the panel would otherwise misreport).
constexpr uint32_t kInitialSubdivision = 256;

class WorldCreatorScene : public engine::IScene {
  public:
	const char* getName() const override { return kSceneName; }
	std::string exportState() override {
		return std::format(
			R"({{"scene":"world_creator","state":{},"globe":{}}})",
			static_cast<int>(model.getState()),
			globe.isReady() ? "true" : "false");
	}

	void onEnter() override {
		LOG_INFO(Game, "WorldCreatorScene - Entering");

		int vpW = 0;
		int vpH = 0;
		Renderer::Primitives::getLogicalViewport(vpW, vpH);
		viewportW = static_cast<float>(vpW);
		viewportH = static_cast<float>(vpH);

		buildUI();

		// Returning from landing site selection: restore the generated world
		// into Reviewing instead of starting over
		if (world_sim::GameStartConfig::HasPending()) {
			auto config = world_sim::GameStartConfig::Take();
			if (config && config->world) {
				LOG_INFO(Game, "WorldCreatorScene: restoring generated world for review");
				model.restoreResult(config->world);
				syncPanelFromModel();
				if (panel) {
					panel->setResolutionValue(
						std::to_string(model.getParams().gridSubdivision));
				}
				lastSnapshot = config->world;
				globe.setWorld(config->world);
				onStateChanged(world_sim::WorldCreatorState::Reviewing);
			}
		}
	}

	void onExit() override {
		LOG_INFO(Game, "WorldCreatorScene - Exiting");
		panel.reset();
		progressBar.reset();
		stageText.reset();
		landingButton.reset();
	}

	bool handleInput(UI::InputEvent& event) override {
		// Mid-drag the globe owns the mouse: widgets must not see the moves
		// (hover flicker) and the drag-ending MouseUp belongs to the camera
		if (globe.isDragging() && globe.handleInput(event, mainRect(), true)) {
			return true;
		}
		if (panel) {
			if (panel->handleEvent(event)) return true;
		}
		if (landingButton && landingButton->visible) {
			if (landingButton->handleEvent(event)) return true;
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

			// Upload colors progressively as stages publish snapshots
			auto snap = model.snapshot();
			if (snap && snap != lastSnapshot) {
				lastSnapshot = snap;
				globe.setWorld(snap);
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
		}

		if (panel) { panel->update(dt); }
		if (landingButton) { landingButton->update(dt); }
	}

	void render() override {
		glClearColor(0.08F, 0.07F, 0.12F, 1.0F);
		glClear(GL_COLOR_BUFFER_BIT);

		Foundation::Rect rect = mainRect();

		// 3D pass first: Primitives batches flush after the scene, so all 2D
		// UI composites on top of the blitted globe.
		if (globe.isReady()) {
			globe.render(rect, viewportW, viewportH);
		} else {
			renderPlaceholder(rect);
		}

		renderTitle();

		if (panel) { panel->render(); }

		auto state = model.getState();
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

		if (globe.isReady()) {
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
	std::unique_ptr<UI::Button>      landingButton;

	world_sim::GlobeView globe;
	std::shared_ptr<const worldgen::GeneratedWorld> lastSnapshot;
	std::string errorText;

	// Main content area right of the parameter panel. Bottom strip (80px)
	// holds the progress bar / stage text / stats / action buttons.
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

		// Reviewing-state action. Regeneration is driven by the panel's Generate
		// button (the single generate path), so the bottom strip only advances to
		// landing-site selection.
		landingButton = std::make_unique<UI::Button>(UI::Button::Args{
			.label = "Choose Landing Site",
			.position = {mainX, viewportH - 50.0F},
			.size = {200.0F, 36.0F},
			.type = UI::Button::Type::Primary,
			.onClick = [this]() { onChooseLandingSite(); },
			.id = "btn_choose_landing",
		});
		landingButton->visible = false;
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
		lastSnapshot.reset();

		model.startGeneration();
		if (panel) panel->setGenerating(true);
		if (progressBar) { progressBar->setValue(0.0F); progressBar->visible = true; }
		if (stageText)   { stageText->text = "Starting..."; stageText->visible = true; }
		if (landingButton) landingButton->visible = false;
	}

	void onStateChanged(world_sim::WorldCreatorState newState) {
		if (newState == world_sim::WorldCreatorState::Reviewing) {
			if (panel)         panel->setGenerating(false);
			if (progressBar)   progressBar->visible = false;
			if (stageText)     stageText->visible = false;
			if (landingButton) landingButton->visible = true;

			// Final colors from the completed world
			if (auto result = model.getResult()) {
				lastSnapshot = result;
				globe.setWorld(result);
			}
		} else if (newState == world_sim::WorldCreatorState::Configuring) {
			if (panel)         panel->setGenerating(false);
			if (progressBar)   progressBar->visible = false;
			if (stageText)     stageText->visible = false;
			if (landingButton) landingButton->visible = false;
		}
	}

	void onChooseLandingSite() {
		auto result = model.getResult();
		if (!result) {
			return;
		}
		LOG_INFO(Game, "WorldCreatorScene: continuing to landing site selection");
		auto config = std::make_unique<world_sim::GameStartConfig>();
		config->source = world_sim::GameStartConfig::Source::NewGame;
		config->world = result;
		world_sim::GameStartConfig::SetPending(std::move(config));
		sceneManager->switchTo(world_sim::toKey(world_sim::SceneType::LandingSite));
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
			"   (right-click to cycle, drag to orbit, scroll to zoom)";
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
		// Only report stats whose underlying fields the pipeline actually
		// produced; unimplemented stages must not masquerade as data.
		auto worldResult = model.getResult();
		std::string statsStr;
		if (worldResult) {
			auto hasField = [&](worldgen::WorldField f) {
				return (worldResult->validFields & static_cast<uint32_t>(f)) != 0;
			};
			const auto& summary = worldResult->summary;
			std::vector<std::string> parts;
			if (hasField(worldgen::WorldField::Elevation)) {
				parts.push_back(std::format("Land: {:.0f}%", summary.landFraction * 100.0F));
			}
			if (hasField(worldgen::WorldField::TemperatureMean)) {
				parts.push_back(std::format("Mean Temp: {:.1f}C", summary.meanTemperatureC));
			}
			if (hasField(worldgen::WorldField::Biome)) {
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

		if (landingButton) { landingButton->render(); }
	}
};

} // namespace

namespace world_sim::scenes {
	extern const world_sim::SceneInfo WorldCreator = {kSceneName, []() {
		return std::make_unique<WorldCreatorScene>();
	}};
}
