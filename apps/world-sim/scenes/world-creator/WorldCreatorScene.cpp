// World Creator Scene
// Three states: Configuring (parameter panel), Generating (progress), Reviewing (results).

#include "SceneTypes.h"
#include "WorldCreatorModel.h"
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

namespace {

constexpr const char* kSceneName = "world_creator";
constexpr float kPanelWidth = 320.0F;
constexpr float kProgressBarHeight = 16.0F;

class WorldCreatorScene : public engine::IScene {
  public:
	const char* getName() const override { return kSceneName; }
	std::string exportState() override { return R"({"scene":"world_creator"})"; }

	void onEnter() override {
		LOG_INFO(Game, "WorldCreatorScene - Entering");

		int vpW = 0;
		int vpH = 0;
		Renderer::Primitives::getLogicalViewport(vpW, vpH);
		viewportW = static_cast<float>(vpW);
		viewportH = static_cast<float>(vpH);

		buildUI();
	}

	void onExit() override {
		LOG_INFO(Game, "WorldCreatorScene - Exiting");
		panel.reset();
		progressBar.reset();
		stageText.reset();
		regenButton.reset();
	}

	bool handleInput(UI::InputEvent& event) override {
		if (panel) {
			if (panel->handleEvent(event)) return true;
		}
		if (regenButton && regenButton->visible) {
			if (regenButton->handleEvent(event)) return true;
		}
		return false;
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
				onStateChanged(newState);
			}
		}

		if (panel) { panel->update(dt); }
		if (regenButton) { regenButton->update(dt); }
	}

	void render() override {
		glClearColor(0.08F, 0.07F, 0.12F, 1.0F);
		glClear(GL_COLOR_BUFFER_BIT);

		renderTitle();

		auto state = model.getState();

		// Parameter panel always visible
		if (panel) { panel->render(); }

		if (state == world_sim::WorldCreatorState::Generating) {
			renderGeneratingOverlay();
		} else if (state == world_sim::WorldCreatorState::Reviewing) {
			renderReviewingView();
		} else {
			renderConfigPlaceholder();
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
	std::unique_ptr<UI::Button>      regenButton;

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

		// Regenerate button (only in Reviewing state)
		regenButton = std::make_unique<UI::Button>(UI::Button::Args{
			.label = "Regenerate",
			.position = {mainX, viewportH - 50.0F},
			.size = {160.0F, 36.0F},
			.type = UI::Button::Type::Secondary,
			.onClick = [this]() { onRegenerate(); },
			.id = "btn_regenerate",
		});
		regenButton->visible = false;
	}

	void startGeneration() {
		model.startGeneration();
		if (panel) panel->setGenerating(true);
		if (progressBar) { progressBar->setValue(0.0F); progressBar->visible = true; }
		if (stageText)   { stageText->text = "Starting..."; stageText->visible = true; }
		if (regenButton) regenButton->visible = false;
	}

	void onStateChanged(world_sim::WorldCreatorState newState) {
		if (newState == world_sim::WorldCreatorState::Reviewing) {
			if (panel)       panel->setGenerating(false);
			if (progressBar) progressBar->visible = false;
			if (stageText)   stageText->visible = false;
			if (regenButton) regenButton->visible = true;
		} else if (newState == world_sim::WorldCreatorState::Configuring) {
			if (panel)       panel->setGenerating(false);
			if (progressBar) progressBar->visible = false;
			if (stageText)   stageText->visible = false;
			if (regenButton) regenButton->visible = false;
		}
	}

	void onRegenerate() {
		model.resetToConfiguring();
		if (regenButton) regenButton->visible = false;
		if (panel)       panel->setGenerating(false);
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

	void renderConfigPlaceholder() {
		float mainX = kPanelWidth + 20.0F;
		float mainW = viewportW - mainX - 20.0F;
		float mainH = viewportH - 80.0F;

		Renderer::Primitives::drawRect({
			.bounds = {mainX, 40.0F, mainW, mainH},
			.style = {
				.fill = Foundation::Color{0.05F, 0.05F, 0.08F, 1.0F},
				.border = Foundation::BorderStyle{
					.color = Foundation::Color{0.2F, 0.2F, 0.25F, 1.0F},
					.width = 1.0F,
				},
			},
			.id = "planet_view_placeholder",
		});

		UI::Text placeholder(UI::Text::Args{
			.position = {mainX + mainW * 0.5F, 40.0F + mainH * 0.5F},
			.text = "Planet view coming in M5",
			.style = {
				.color = Foundation::Color{0.3F, 0.3F, 0.35F, 1.0F},
				.fontSize = 18.0F,
				.hAlign = Foundation::HorizontalAlign::Center,
				.vAlign = Foundation::VerticalAlign::Middle,
			},
		});
		placeholder.render();
	}

	void renderGeneratingOverlay() {
		float mainX = kPanelWidth + 20.0F;
		float mainW = viewportW - mainX - 20.0F;
		float mainH = viewportH - 80.0F;

		Renderer::Primitives::drawRect({
			.bounds = {mainX, 40.0F, mainW, mainH},
			.style = {
				.fill = Foundation::Color{0.04F, 0.04F, 0.07F, 1.0F},
				.border = Foundation::BorderStyle{
					.color = Foundation::Color{0.2F, 0.2F, 0.25F, 1.0F},
					.width = 1.0F,
				},
			},
			.id = "generating_bg",
		});

		UI::Text genLabel(UI::Text::Args{
			.position = {mainX + mainW * 0.5F, 40.0F + mainH * 0.5F - 20.0F},
			.text = "Generating World...",
			.style = {
				.color = Foundation::Color{0.7F, 0.75F, 1.0F, 1.0F},
				.fontSize = 22.0F,
				.hAlign = Foundation::HorizontalAlign::Center,
				.vAlign = Foundation::VerticalAlign::Middle,
			},
		});
		genLabel.render();

		if (stageText)   { stageText->render(); }
		if (progressBar) { progressBar->render(); }
	}

	void renderReviewingView() {
		float mainX = kPanelWidth + 20.0F;
		float mainW = viewportW - mainX - 20.0F;
		float mainH = viewportH - 120.0F;

		Renderer::Primitives::drawRect({
			.bounds = {mainX, 40.0F, mainW, mainH},
			.style = {
				.fill = Foundation::Color{0.05F, 0.05F, 0.08F, 1.0F},
				.border = Foundation::BorderStyle{
					.color = Foundation::Color{0.2F, 0.35F, 0.25F, 1.0F},
					.width = 1.0F,
				},
			},
			.id = "review_planet_placeholder",
		});

		UI::Text ready(UI::Text::Args{
			.position = {mainX + mainW * 0.5F, 40.0F + mainH * 0.4F},
			.text = "World Generated",
			.style = {
				.color = Foundation::Color{0.4F, 0.9F, 0.5F, 1.0F},
				.fontSize = 26.0F,
				.hAlign = Foundation::HorizontalAlign::Center,
				.vAlign = Foundation::VerticalAlign::Middle,
			},
		});
		ready.render();

		// Stats from completed world summary
		auto worldResult = model.getResult();
		std::string statsStr = "Generating...";
		if (worldResult) {
			const auto& summary = worldResult->summary;
			statsStr = std::format(
				"Land: {:.0f}%  |  Mean Temp: {:.1f}C  |  Habitability: {:.0f}%",
				summary.landFraction * 100.0F,
				summary.meanTemperatureC,
				summary.habitability * 100.0F);
		}

		UI::Text stats(UI::Text::Args{
			.position = {mainX + mainW * 0.5F, 40.0F + mainH * 0.4F + 36.0F},
			.text = statsStr,
			.style = {
				.color = Foundation::Color{0.75F, 0.75F, 0.75F, 1.0F},
				.fontSize = 15.0F,
				.hAlign = Foundation::HorizontalAlign::Center,
				.vAlign = Foundation::VerticalAlign::Middle,
			},
		});
		stats.render();

		if (regenButton) { regenButton->render(); }
	}
};

} // namespace

namespace world_sim::scenes {
	extern const world_sim::SceneInfo WorldCreator = {kSceneName, []() {
		return std::make_unique<WorldCreatorScene>();
	}};
}
