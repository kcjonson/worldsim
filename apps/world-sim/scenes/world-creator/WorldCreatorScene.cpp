// World Creator Scene
// Three states: Configuring (parameter panel), Generating (progress + live globe),
// Reviewing (final globe + stats). The 3D planet view renders into the main area
// right of the parameter panel; 2D UI composites on top of it.

#include "SceneTypes.h"
#include "WorldCreatorModel.h"
#include "ui/ParameterPanel.h"

#include <GL/glew.h>

#include <planet-view/OrbitCamera.h>
#include <planet-view/PlanetColorizer.h>
#include <planet-view/PlanetMesh.h>
#include <planet-view/PlanetRenderer.h>

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
#include <utils/ResourcePath.h>

#include <algorithm>
#include <format>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr const char* kSceneName = "world_creator";
constexpr float kPanelWidth = 320.0F;
constexpr float kProgressBarHeight = 16.0F;
// Colorizer texel work scales with texSize^2 per snapshot; clamp so High-res
// grids (1449) don't hitch the UI thread for seconds on every stage publish.
constexpr uint32_t kMaxColorTexSize = 1024;

class WorldCreatorScene : public engine::IScene {
  public:
	const char* getName() const override { return kSceneName; }
	std::string exportState() override {
		return std::format(
			R"({{"scene":"world_creator","state":{},"globe":{}}})",
			static_cast<int>(model.getState()),
			meshBuilt ? "true" : "false");
	}

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
		return handleGlobeInput(event);
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

		camera.update(dt);

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
				onSnapshot(*snap);
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
		if (regenButton) { regenButton->update(dt); }
	}

	void render() override {
		glClearColor(0.08F, 0.07F, 0.12F, 1.0F);
		glClear(GL_COLOR_BUFFER_BIT);

		Foundation::Rect rect = mainRect();
		bool globeVisible = meshBuilt && renderer.isReady() && colorizer.isReady();

		// 3D pass first: Primitives batches flush after the scene, so all 2D
		// UI composites on top of the blitted globe.
		if (globeVisible) {
			renderGlobe(rect);
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

		if (globeVisible) {
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
	std::unique_ptr<UI::Button>      regenButton;

	planetview::PlanetMesh      mesh;
	planetview::PlanetColorizer colorizer;
	planetview::PlanetRenderer  renderer;
	planetview::OrbitCamera     camera;

	std::shared_ptr<const worldgen::GeneratedWorld> lastSnapshot;
	bool meshBuilt{false};
	bool draggingGlobe{false};
	int  colorModeIdx{static_cast<int>(planetview::ColorMode::Terrain)};
	std::string errorText;

	// Main content area right of the parameter panel. Bottom strip (80px)
	// holds the progress bar / stage text / stats / regenerate button.
	Foundation::Rect mainRect() const {
		float x = kPanelWidth + 20.0F;
		return {x, 40.0F, viewportW - x - 20.0F, viewportH - 120.0F};
	}

	bool inMainRect(Foundation::Vec2 p) const {
		Foundation::Rect r = mainRect();
		return p.x >= r.x && p.x <= r.x + r.width && p.y >= r.y && p.y <= r.y + r.height;
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

		// Blank seed means "surprise me": pick a fresh seed each run so Regenerate
		// doesn't silently reproduce the identical world from the preset default.
		if (panel && panel->seedIsEmpty()) {
			model.randomizeSeed();
			syncPanelFromModel();
		}

		errorText.clear();

		// New generation means a new grid; rebuild mesh from the first snapshot
		meshBuilt = false;
		lastSnapshot.reset();

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

			// Final colors from the completed world
			if (auto result = model.getResult()) {
				lastSnapshot = result;
				onSnapshot(*result);
			}
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

	// Called for each progressive snapshot and the final result.
	void onSnapshot(const worldgen::GeneratedWorld& world) {
		if (!world.grid) return;

		if (!meshBuilt) {
			uint32_t subdiv = world.grid->subdivision();
			mesh.build(subdiv, *world.grid);
			colorizer.init(std::min(subdiv, kMaxColorTexSize));

			if (!renderer.isReady()) {
				std::string shaderDir = Foundation::findResourceString("shaders");
				if (shaderDir.empty()) shaderDir = "shaders";

				Foundation::Rect rect = mainRect();
				if (!renderer.init(shaderDir.c_str(),
				                   static_cast<int>(rect.width),
				                   static_cast<int>(rect.height))) {
					LOG_ERROR(Game, "WorldCreatorScene: planet renderer init failed");
					return;
				}
			}

			meshBuilt = true;
			LOG_INFO(Game, "WorldCreatorScene: globe mesh ready (n=%u)", subdiv);
		}

		if (colorizer.isReady()) {
			colorizer.update(world, static_cast<planetview::ColorMode>(colorModeIdx));
		}
	}

	void switchColorMode(int idx) {
		colorModeIdx = idx % static_cast<int>(planetview::ColorMode::Count);
		if (lastSnapshot && colorizer.isReady()) {
			colorizer.update(*lastSnapshot, static_cast<planetview::ColorMode>(colorModeIdx));
		}
	}

	bool handleGlobeInput(UI::InputEvent& event) {
		if (!meshBuilt) return false;

		switch (event.type) {
			case UI::InputEvent::Type::MouseDown:
				if (!inMainRect(event.position)) break;
				if (event.button == engine::MouseButton::Left) {
					camera.beginDrag(event.position.x, event.position.y);
					draggingGlobe = true;
					event.consume();
					return true;
				}
				if (event.button == engine::MouseButton::Right) {
					switchColorMode(colorModeIdx + 1);
					event.consume();
					return true;
				}
				break;

			case UI::InputEvent::Type::MouseMove:
				if (draggingGlobe) {
					camera.drag(event.position.x, event.position.y);
					return true;
				}
				break;

			case UI::InputEvent::Type::MouseUp:
				if (draggingGlobe && event.button == engine::MouseButton::Left) {
					camera.endDrag();
					draggingGlobe = false;
					event.consume();
					return true;
				}
				break;

			case UI::InputEvent::Type::Scroll:
				if (inMainRect(event.position)) {
					camera.scroll(event.scrollDelta);
					event.consume();
					return true;
				}
				break;

			default:
				break;
		}
		return false;
	}

	// Render the globe FBO and blit it into the main rect's viewport region.
	void renderGlobe(const Foundation::Rect& rect) {
		GLint vp[4] = {};
		glGetIntegerv(GL_VIEWPORT, vp);

		float sx = viewportW > 0.0F ? static_cast<float>(vp[2]) / viewportW : 1.0F;
		float sy = viewportH > 0.0F ? static_cast<float>(vp[3]) / viewportH : 1.0F;
		int px = static_cast<int>(rect.x * sx);
		int py = static_cast<int>(rect.y * sy);
		int pw = static_cast<int>(rect.width * sx);
		int ph = static_cast<int>(rect.height * sy);
		if (pw <= 0 || ph <= 0) return;

		renderer.render(mesh, colorizer, camera, pw, ph);

		// GL viewport origin is bottom-left; UI rect origin is top-left
		glViewport(px, vp[3] - py - ph, pw, ph);
		renderer.blitToScreen(pw, ph);
		glViewport(vp[0], vp[1], vp[2], vp[3]);
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
			planetview::colorModeName(static_cast<planetview::ColorMode>(colorModeIdx)) +
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

		if (regenButton) { regenButton->render(); }
	}
};

} // namespace

namespace world_sim::scenes {
	extern const world_sim::SceneInfo WorldCreator = {kSceneName, []() {
		return std::make_unique<WorldCreatorScene>();
	}};
}
