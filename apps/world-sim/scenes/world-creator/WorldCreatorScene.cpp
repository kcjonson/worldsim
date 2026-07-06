// World Creator Scene
// Three model states over a shared starfield -- Configuring (parameter panel
// + decorative planet), Generating (progress strip), Reviewing (final globe)
// -- in a 320 | stage | 320 grid with a survey panel on the right and a
// Back / status / actions footer bar. Accepting the world enters the landing
// sub-phase in place: two columns (stage | 360 details), a pulsing crosshair
// marker, and a Commit to Descent dialog gating the actual landing.

#include "GameStartConfig.h"
#include "NewGameSetup.h"
#include "SceneTypes.h"
#include "WorldCreatorModel.h"
#include "scenes/landing/LandingSiteDetailsModel.h"
#include "scenes/landing/LandingSiteDetailsPanel.h"
#include "scenes/shared/GlobeView.h"
#include "scenes/shared/Starfield.h"
#include "scenes/shared/UiStateDrain.h"
#include "scenes/shared/Widgets.h"
#include "ui/ParameterPanel.h"
#include "ui/WorldSurveyPanel.h"

#include <GL/glew.h>

#include <worldgen/data/WorldData.h>
#include <worldgen/io/PlanetIO.h>
#include <worldgen/sampling/LandingSite.h>

#include <components/button/Button.h>
#include <components/dialog/Dialog.h>
#include <components/segmentedcontrol/SegmentedControl.h>
#include <graphics/Color.h>
#include <input/InputEvent.h>
#include <input/InputManager.h>
#include <layout/LayoutContainer.h>
#include <primitives/Primitives.h>
#include <scene/Scene.h>
#include <scene/SceneManager.h>
#include <shapes/Shapes.h>
#include <theme/Tokens.h>
#include <theme/Variants.h>
#include <utils/Log.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr const char* kSceneName = "world_creator";
constexpr float kMarginX = 32.0F;
constexpr float kContentTop = 92.0F;
constexpr float kColumnGap = 16.0F;
constexpr float kFooterHeight = 72.0F;
// Keyboard orbit speed (radians/sec) for arrow keys and WASD.
constexpr float kKeyPanRate = 1.2F;
// Creator opens at preview resolution for fast iteration; must match the
// resolution select's initial value in ParameterPanel (PlanetParams defaults
// to 1024, which the panel would otherwise misreport).
constexpr uint32_t kInitialSubdivision = 256;

// Viz switcher options and their color modes (right-click cycling can still
// reach the other analyst modes; the dock then shows no active segment).
constexpr std::array<planetview::ColorMode, 4> kVizModes{
	planetview::ColorMode::Terrain, planetview::ColorMode::Biome,
	planetview::ColorMode::Temperature, planetview::ColorMode::Precipitation};
const std::vector<std::string> kVizLabels{"Terrain", "Biomes", "Temp", "Rain"};

class WorldCreatorScene : public engine::IScene {
  public:
	const char* getName() const override { return kSceneName; }
	std::string exportState() override {
		return std::format(
			R"({{"scene":"world_creator","state":{},"globe":{},"site":{},"landing":{},"confirming":{}}})",
			static_cast<int>(model.getState()),
			globe.isReady() ? "true" : "false",
			siteValid ? "true" : "false",
			landingMode ? "true" : "false",
			(confirmDialog && confirmDialog->isOpen()) ? "true" : "false");
	}

	void onEnter() override {
		LOG_INFO(Game, "WorldCreatorScene - Entering");

		int vpW = 0;
		int vpH = 0;
		Renderer::Primitives::getLogicalViewport(vpW, vpH);
		viewportW = static_cast<float>(vpW);
		viewportH = static_cast<float>(vpH);
		landingMode = false;

		buildUI();

		// Pre-fill a random seed so the field isn't blank on entry.
		// Uses the same mechanism as the Random button.
		model.randomizeSeed();
		syncPanelFromModel();
		refreshFooter();
	}

	void onExit() override {
		LOG_INFO(Game, "WorldCreatorScene - Exiting");
		panel.reset();
		surveyFooter.reset();
		landingFooter.reset();
		backToSurveyButton.reset();
		confirmDialog.reset();
		statusLabel = nullptr;
		footerActions = nullptr;
		generateButton = nullptr;
		cancelButton = nullptr;
		regenerateButton = nullptr;
		acceptButton = nullptr;
		confirmBody = nullptr;
		confirmCancel = nullptr;
		confirmLand = nullptr;
	}

	bool handleInput(UI::InputEvent& event) override {
		if (confirmDialog && confirmDialog->isOpen()) {
			confirmDialog->handleEvent(event);
			return true; // modal: swallow everything while open
		}

		const bool reviewing =
			model.getState() == world_sim::WorldCreatorState::Reviewing;

		// Mid-drag the globe owns the mouse: widgets must not see the moves
		// (hover flicker) and the drag-ending MouseUp belongs to the camera.
		if (reviewing && globe.isDragging() &&
		    globe.handleInput(event, stageRect(), true)) {
			return true;
		}

		if (landingMode && backToSurveyButton && backToSurveyButton->handleEvent(event)) return true;
		if (generateButton && generateButton->visible && generateButton->handleEvent(event)) return true;
		if (cancelButton && cancelButton->visible && cancelButton->handleEvent(event)) return true;
		if (!landingMode && panel && panel->handleEvent(event)) return true;
		if (activeFooter() != nullptr && activeFooter()->handleEvent(event)) return true;

		// Outside Reviewing the globe doesn't exist on screen: nothing to orbit
		// or pick.
		if (!reviewing) return false;

		// Viz dock: a click on a segment switches the globe's color mode.
		if (event.type == UI::InputEvent::Type::MouseUp &&
		    event.button == engine::MouseButton::Left && handleVizClick(event.position)) {
			event.consume();
			return true;
		}

		// Pick before orbit-drag so a left click selects the tile under the
		// cursor; the same click then begins an orbit drag.
		if (event.type == UI::InputEvent::Type::MouseDown &&
		    event.button == engine::MouseButton::Left) {
			if (auto picked = globe.pick(event.position, stageRect())) {
				trySelectSite(*picked);
			}
		}
		return globe.handleInput(event, stageRect(), true);
	}

	void update(float dt) override {
		if (confirmDialog) confirmDialog->update(dt);

		if (engine::InputManager::Get().isKeyPressed(engine::Key::Escape)) {
			if (confirmDialog && confirmDialog->isOpen()) {
				return; // dialog handles its own escape
			}
			if (landingMode) {
				exitLanding();
				return;
			}
			// ESC backs out: Configuring returns to the crew step of the New
			// Game flow; Generating/Reviewing keep bailing to the main menu.
			if (model.getState() == world_sim::WorldCreatorState::Configuring) {
				LOG_INFO(Game, "WorldCreatorScene: returning to party select");
				sceneManager->switchTo(world_sim::toKey(world_sim::SceneType::PartySelect));
				return;
			}
			if (model.getState() == world_sim::WorldCreatorState::Generating) {
				model.cancelGeneration();
			}
			LOG_INFO(Game, "WorldCreatorScene: returning to main menu");
			sceneManager->switchTo(world_sim::toKey(world_sim::SceneType::MainMenu));
			return;
		}

		globe.update(dt);
		markerPulse += dt;

		auto state = model.getState();

		if (state == world_sim::WorldCreatorState::Generating) {
			auto prog = model.pollProgress();
			progressFraction = prog.totalFraction;
			progressStage = prog.stageName != nullptr ? prog.stageName : "";

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

		if (state == world_sim::WorldCreatorState::Configuring && generateButton) {
			generateButton->setDisabled(panel && !panel->seedIsValid());
		}

		if (panel && !landingMode) { panel->update(dt); }
		if (generateButton) generateButton->update(dt);
		if (cancelButton) cancelButton->update(dt);
		if (activeFooter() != nullptr) activeFooter()->update(dt);
		if (landingMode && backToSurveyButton) backToSurveyButton->update(dt);
	}

	void render() override {
		glClearColor(UI::bg_void.r, UI::bg_void.g, UI::bg_void.b, 1.0F);
		glClear(GL_COLOR_BUFFER_BIT);

		int vpW = 0;
		int vpH = 0;
		Renderer::Primitives::getLogicalViewport(vpW, vpH);
		if (vpW > 0 && vpH > 0) {
			viewportW = static_cast<float>(vpW);
			viewportH = static_cast<float>(vpH);
		}

		world_sim::renderStarfield(static_cast<int>(viewportW), static_cast<int>(viewportH), 5U, true);

		Foundation::Rect stage = stageRect();
		auto			 state = model.getState();

		// No planet is shown until generation completes: the stage stays empty
		// while configuring, shows only the progress strip while generating, and
		// reveals the real globe once it is ready. Never a stand-in world.
		const bool showGlobe =
			state == world_sim::WorldCreatorState::Reviewing && globe.isReady();

		// 3D pass first: Primitives batches flush after the scene, so all 2D
		// UI composites on top of the blitted globe. The explicit flush paints
		// the starfield beneath the blit.
		if (showGlobe) {
			Renderer::Primitives::flush();
			globe.render(stage, viewportW, viewportH);
		}

		renderHeader();

		const float columnH = footerTop() - kContentTop - kColumnGap;
		if (!landingMode) {
			if (panel) { panel->render(columnH); }

			// Generate / Cancel pinned full-width to the bottom of the sidebar.
			UI::Button* sideAction = generateButton && generateButton->visible ? generateButton.get()
									 : (cancelButton && cancelButton->visible ? cancelButton.get() : nullptr);
			if (sideAction != nullptr) {
				const float bh = 40.0F;
				sideAction->setPosition(kMarginX + 12.0F, kContentTop + columnH - bh - 12.0F);
				sideAction->render();
			}

			surveyPanel.render({viewportW - kMarginX - world_sim::WorldSurveyPanel::kWidth,
								kContentTop, world_sim::WorldSurveyPanel::kWidth, columnH});
		}

		if (state == world_sim::WorldCreatorState::Generating) {
			renderProgress(stage);
		} else if (state == world_sim::WorldCreatorState::Reviewing) {
			renderReviewing(stage);
		} else if (!errorText.empty()) {
			UI::Text err(UI::Text::Args{
				.position = {stage.x, stage.y + stage.height - 24.0F},
				.text = errorText,
				.style = {
					.color = UI::status_crit,
					.fontSize = UI::fs_md,
					.hAlign = Foundation::HorizontalAlign::Left,
					.vAlign = Foundation::VerticalAlign::Middle,
				},
			});
			err.render();
		}

		renderFooter();

		if (!landingMode) renderEscHint();

		if (confirmDialog && confirmDialog->isOpen()) {
			confirmDialog->render();
		}

		world_sim::serveUiStateRequests(*this);
	}

	std::vector<const UI::IComponent*> getUiRoots() const override {
		std::vector<const UI::IComponent*> roots;
		if (activeFooter() != nullptr) roots.push_back(activeFooter());
		if (generateButton && generateButton->visible) roots.push_back(generateButton.get());
		if (cancelButton && cancelButton->visible) roots.push_back(cancelButton.get());
		if (landingMode && backToSurveyButton) roots.push_back(backToSurveyButton.get());
		if (confirmDialog && confirmDialog->isOpen()) roots.push_back(confirmDialog.get());
		return roots;
	}

  private:
	world_sim::WorldCreatorModel model;

	float viewportW{1280.0F};
	float viewportH{720.0F};

	std::unique_ptr<world_sim::ParameterPanel> panel;
	world_sim::WorldSurveyPanel				   surveyPanel;

	// Footer bars: survey (Back | status | Generate/Cancel/Regenerate+Accept)
	// and landing (note | Confirm Landing Site).
	std::unique_ptr<UI::LayoutContainer> surveyFooter;
	std::unique_ptr<UI::LayoutContainer> landingFooter;
	world_sim::Label*					 statusLabel{nullptr};
	UI::LayoutContainer*				 footerActions{nullptr};
	std::unique_ptr<UI::Button>			 generateButton; // bottom of the sidebar
	std::unique_ptr<UI::Button>			 cancelButton;	 // bottom of the sidebar
	UI::Button*							 regenerateButton{nullptr};
	UI::Button*							 acceptButton{nullptr};
	std::unique_ptr<UI::Button>			 backToSurveyButton;
	std::unique_ptr<UI::Dialog>			 confirmDialog;
	UI::Text*							 confirmBody{nullptr};
	UI::Button*							 confirmCancel{nullptr};
	UI::Button*							 confirmLand{nullptr};
	Foundation::Vec2					 footerLayoutViewport{0.0F, 0.0F};

	world_sim::GlobeView globe;
	std::string errorText;
	float		progressFraction{0.0F};
	std::string progressStage;
	float		markerPulse{0.0F};

	// Landing selection (Reviewing only). The site auto-suggests on completion
	// and updates on each land-tile click.
	planetview::LatLon                 selectedSite{};
	bool                               siteValid{false};
	bool                               landingMode{false};
	std::string                        pickHint;
	world_sim::LandingSiteDetails      details;
	world_sim::LandingSiteDetailsPanel detailsPanel;

	float footerTop() const { return viewportH - kFooterHeight; }

	// Stage between the side columns (survey) or left of the details column
	// (landing).
	Foundation::Rect stageRect() const {
		const float top = kContentTop;
		const float height = footerTop() - kColumnGap - top;
		if (landingMode) {
			const float x = kMarginX;
			const float width =
				viewportW - x - world_sim::LandingSiteDetailsPanel::kWidth - kColumnGap - kMarginX;
			return {x, top, width, height};
		}
		const float x = kMarginX + world_sim::ParameterPanel::kPanelWidth + kColumnGap;
		const float width =
			viewportW - x - world_sim::WorldSurveyPanel::kWidth - kColumnGap - kMarginX;
		return {x, top, width, height};
	}

	UI::LayoutContainer* activeFooter() const {
		return landingMode ? landingFooter.get() : surveyFooter.get();
	}

	void buildUI() {
		using namespace UI;

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

		panel = std::make_unique<world_sim::ParameterPanel>(
			Foundation::Vec2{kMarginX, kContentTop}, std::move(cbs));
		model.setGridSubdivision(kInitialSubdivision);
		surveyPanel.setWorld(nullptr);

		// Survey footer: Back | status | actions.
		surveyFooter = std::make_unique<LayoutContainer>(LayoutContainer::Args{
			.size = {1.0F, 40.0F},
			.direction = Direction::Horizontal,
			.gap = space_4,
			.crossAlign = CrossAlign::Center,
			.id = "wc_footer"});
		surveyFooter->addChild(Button(Button::Args{
			.label = "Back",
			.size = {120.0F, 40.0F},
			.type = Button::Type::Secondary,
			.onClick = [this]() { goBackToParty(); },
			.id = "btn_wc_back",
			.iconGlyph = "chevronLeft"}));
		world_sim::Label status({
			.text = "Awaiting generation",
			.fontSize = fs_xs,
			.color = text_dim,
			.font = fontMono,
			.letterSpacingEm = ls_wide,
			.hAlign = Foundation::HorizontalAlign::Center,
			.id = "wc_status"});
		status.widthMode = SizeMode::Fill;
		LayerHandle statusHandle = surveyFooter->addChild(std::move(status));

		// Fixed width + End distribution: swapping which action buttons are
		// visible re-lays inside a stable box (a Hug box would ratchet to the
		// first layout's width and overflow when wider sets appear).
		LayoutContainer actions(LayoutContainer::Args{
			.size = {350.0F, 40.0F},
			.direction = Direction::Horizontal, .gap = space_3,
			.distribution = Distribution::End, .crossAlign = CrossAlign::Center,
			.id = "wc_footer_actions"});
		LayerHandle regenHandle = actions.addChild(Button(Button::Args{
			.label = "Regenerate",
			.size = {150.0F, 40.0F},
			.type = Button::Type::Ghost,
			.onClick = [this]() { startGeneration(); },
			.id = "btn_wc_regenerate",
			.iconGlyph = "refresh"}));
		LayerHandle acceptHandle = actions.addChild(Button(Button::Args{
			.label = "Accept World",
			.size = {180.0F, 40.0F},
			.type = Button::Type::Primary,
			.onClick = [this]() { enterLanding(); },
			.id = "btn_wc_accept",
			.iconGlyph = "arrowRight"}));
		LayerHandle actionsHandle = surveyFooter->addChild(std::move(actions));

		footerActions = surveyFooter->getChild<LayoutContainer>(actionsHandle);
		statusLabel = surveyFooter->getChild<world_sim::Label>(statusHandle);
		regenerateButton = footerActions->getChild<Button>(regenHandle);
		acceptButton = footerActions->getChild<Button>(acceptHandle);

		// Generate / Cancel live at the bottom of the parameter sidebar, not the
		// footer. Full sidebar width; positioned in render(). The scene owns them.
		const float sideW = world_sim::ParameterPanel::kPanelWidth - 24.0F;
		generateButton = std::make_unique<Button>(Button::Args{
			.label = "Generate",
			.size = {sideW, 40.0F},
			.type = Button::Type::Primary,
			.onClick = [this]() { startGeneration(); },
			.id = "btn_wc_generate",
			.iconGlyph = "globe"});
		cancelButton = std::make_unique<Button>(Button::Args{
			.label = "Cancel",
			.size = {sideW, 40.0F},
			.type = Button::Type::Secondary,
			.onClick = [this]() { model.cancelGeneration(); },
			.id = "btn_wc_cancel"});

		// Landing footer: note | Confirm Landing Site.
		landingFooter = std::make_unique<LayoutContainer>(LayoutContainer::Args{
			.size = {1.0F, 40.0F},
			.direction = Direction::Horizontal,
			.gap = space_4,
			.crossAlign = CrossAlign::Center,
			.id = "landing_footer"});
		world_sim::Label note({
			.text = "You can land anywhere on solid ground. Choose well - there is no second descent.",
			.fontSize = fs_xs,
			.color = text_dim,
			.font = fontUi,
			.id = "landing_note"});
		note.widthMode = SizeMode::Fill;
		landingFooter->addChild(std::move(note));
		landingFooter->addChild(Button(Button::Args{
			.label = "Confirm Landing Site",
			.size = {240.0F, 40.0F},
			.type = Button::Type::Primary,
			.onClick = [this]() { openConfirmDialog(); },
			.id = "btn_confirm_landing",
			.iconGlyph = "rocket"}));

		backToSurveyButton = std::make_unique<Button>(Button::Args{
			.label = "Back to Survey",
			.size = {180.0F, 36.0F},
			.type = Button::Type::Ghost,
			.onClick = [this]() { exitLanding(); },
			.id = "btn_back_to_survey",
			.iconGlyph = "chevronLeft"});

		// Commit-to-descent confirmation.
		confirmDialog = std::make_unique<Dialog>(Dialog::Args{
			.title = "Commit to Descent",
			.kicker = "Confirm",
			.size = {440.0F, 216.0F},
			.modal = true});
		confirmDialog->zIndex = static_cast<short>(z_modal); // overlay layer for the lint's sibling rule
		LayerHandle bodyHandle = confirmDialog->addChild(Text(Text::Args{
			.text = "",
			.style = {.color = text, .fontSize = fs_md, .wordWrap = true},
			.id = "confirm_body"}));
		LayerHandle cancelBtnHandle = confirmDialog->addChild(Button(Button::Args{
			.label = "Cancel",
			.size = {110.0F, 36.0F},
			.type = Button::Type::Secondary,
			.onClick = [this]() { confirmDialog->close(); },
			.id = "btn_descent_cancel"}));
		LayerHandle landBtnHandle = confirmDialog->addChild(Button(Button::Args{
			.label = "Begin Descent",
			.size = {170.0F, 36.0F},
			.type = Button::Type::Primary,
			.onClick = [this]() {
				confirmDialog->close();
				land();
			},
			.id = "btn_begin_descent",
			.iconGlyph = "rocket"}));
		confirmBody = confirmDialog->getChild<Text>(bodyHandle);
		confirmCancel = confirmDialog->getChild<Button>(cancelBtnHandle);
		confirmLand = confirmDialog->getChild<Button>(landBtnHandle);
	}

	// Show/hide footer actions for the current model state.
	void refreshFooter() {
		const auto state = model.getState();
		if (generateButton != nullptr) {
			generateButton->visible = state == world_sim::WorldCreatorState::Configuring;
		}
		if (cancelButton != nullptr) {
			cancelButton->visible = state == world_sim::WorldCreatorState::Generating;
		}
		const bool reviewing = state == world_sim::WorldCreatorState::Reviewing;
		if (regenerateButton != nullptr) regenerateButton->visible = reviewing;
		if (acceptButton != nullptr) acceptButton->visible = reviewing;

		if (statusLabel != nullptr) {
			switch (state) {
				case world_sim::WorldCreatorState::Configuring:
					statusLabel->setText("Awaiting generation");
					statusLabel->setColor(UI::text_dim);
					break;
				case world_sim::WorldCreatorState::Generating:
					statusLabel->setText("Generating world...");
					statusLabel->setColor(UI::text_dim);
					break;
				case world_sim::WorldCreatorState::Reviewing:
					statusLabel->setText("World ready - suitable for colonization");
					statusLabel->setColor(UI::status_ok);
					break;
			}
		}
		// Visibility flips change which children the layout places, so both
		// the row and its nested actions group must re-lay.
		if (footerActions != nullptr) footerActions->invalidateLayout();
		if (surveyFooter) surveyFooter->invalidateLayout();
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
		landingMode = false;
		pickHint.clear();
		progressFraction = 0.0F;
		progressStage = "Starting...";

		model.startGeneration();
		if (panel) panel->setGenerating(true);
		surveyPanel.setWorld(nullptr);
		refreshFooter();
	}

	void onStateChanged(world_sim::WorldCreatorState newState) {
		if (newState == world_sim::WorldCreatorState::Reviewing) {
			if (panel) panel->setGenerating(false);

			if (auto result = model.getResult()) {
				globe.setWorld(result);
				surveyPanel.setWorld(result);
				// Suggest a habitable starting point so the pane has content the
				// moment review begins; the player can click to choose another.
				auto suggested = worldgen::findDefaultLandingSite(*result);
				selectedSite = {static_cast<float>(suggested.latDeg),
				                static_cast<float>(suggested.lonDeg)};
				siteValid = true;
				pickHint.clear();
				refreshDetails();
			}
		} else if (newState == world_sim::WorldCreatorState::Configuring) {
			if (panel) panel->setGenerating(false);
			siteValid = false;
			landingMode = false;
		}
		refreshFooter();
	}

	void enterLanding() {
		if (!siteValid) return;
		landingMode = true;
		LOG_INFO(Game, "WorldCreatorScene: entering landing site selection");
	}

	void exitLanding() {
		landingMode = false;
	}

	void openConfirmDialog() {
		if (!siteValid || confirmDialog == nullptr) return;
		if (confirmBody != nullptr) {
			confirmBody->text = std::format(
				"Your colony will begin at {} - {}. Once you commit, there is no turning back.",
				details.coords, details.biomeName.empty() ? "unknown terrain" : details.biomeName);
		}
		confirmDialog->open(viewportW, viewportH);
		// Dialog children live in content-local coordinates (the content
		// offset maps (0,0) to the content top-left, and children clip to it).
		const Foundation::Rect content = confirmDialog->getContentBounds();
		if (confirmBody != nullptr) {
			confirmBody->position = {0.0F, 0.0F};
			confirmBody->setLayoutSize(content.width, UI::kSizeKeep);
		}
		if (confirmLand != nullptr) {
			confirmLand->setPosition(content.width - confirmLand->size.x,
									 content.height - confirmLand->size.y);
			if (confirmCancel != nullptr) {
				confirmCancel->setPosition(confirmLand->getPosition().x - 12.0F - confirmCancel->size.x,
										   content.height - confirmCancel->size.y);
			}
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
		config->party = world_sim::NewGameSetup::Get().party;
		world_sim::GameStartConfig::SetPending(std::move(config));
		sceneManager->switchTo(world_sim::toKey(world_sim::SceneType::GameLoading));
	}

	void goBackToParty() {
		if (model.getState() == world_sim::WorldCreatorState::Generating) {
			model.cancelGeneration();
		}
		sceneManager->switchTo(world_sim::toKey(world_sim::SceneType::PartySelect));
	}

	void renderHeader() {
		const float kickerY = 20.0F;
		const float titleY = kickerY + UI::fs_2xs + 6.0F;
		Renderer::Primitives::drawText({
			.text = landingMode ? "// EXPEDITION    FINAL APPROACH" : "// NEW GAME    STEP 03 / 03",
			.position = {kMarginX, kickerY},
			.scale = UI::fs_2xs / 16.0F,
			.color = UI::text_faint,
			.font = UI::fontMono,
			.vAlign = Foundation::VerticalAlign::Top,
			.letterSpacing = UI::fs_2xs * UI::ls_wider,
		});
		Renderer::Primitives::drawText({
			.text = landingMode ? "Select Landing Site" : "Generate Planet",
			.position = {kMarginX, titleY},
			.scale = UI::fs_3xl / 16.0F,
			.color = UI::text_bright,
			.font = UI::fontDisplay,
			.vAlign = Foundation::VerticalAlign::Top,
			.letterSpacing = UI::fs_3xl * UI::ls_wide,
		});

		if (landingMode && backToSurveyButton) {
			backToSurveyButton->setPosition(viewportW - kMarginX - backToSurveyButton->size.x, kickerY);
			backToSurveyButton->render();
		}
	}

	void renderEscHint() {
		const bool configuring =
			model.getState() == world_sim::WorldCreatorState::Configuring;
		UI::Text hint(UI::Text::Args{
			.position = {viewportW - kMarginX, 26.0F},
			.text = configuring ? "ESC: Back to crew" : "ESC: Back to menu",
			.style = {
				.color = UI::text_dim,
				.fontSize = UI::fs_base,
				.hAlign = Foundation::HorizontalAlign::Right,
				.vAlign = Foundation::VerticalAlign::Middle,
			},
		});
		hint.render();
	}

	// Centered 3px progress strip: teal stage label left, amber percent right.
	void renderProgress(const Foundation::Rect& stage) {
		using Renderer::Primitives::drawRect;
		using Renderer::Primitives::drawText;

		const float barW = std::min(420.0F, stage.width * 0.8F);
		const float barX = stage.x + (stage.width - barW) * 0.5F;
		const float barY = stage.y + stage.height - UI::space_12;

		drawText({.text = progressStage,
				  .position = {barX, barY - 20.0F},
				  .scale = UI::fs_xs / 16.0F,
				  .color = UI::data_bright,
				  .font = UI::fontMono,
				  .vAlign = Foundation::VerticalAlign::Top});
		drawText({.text = std::format("{}%", static_cast<int>(progressFraction * 100.0F)),
				  .position = {barX, barY - 20.0F},
				  .scale = UI::fs_xs / 16.0F,
				  .color = UI::accent_bright,
				  .font = UI::fontMono,
				  .hAlign = Foundation::HorizontalAlign::Right,
				  .vAlign = Foundation::VerticalAlign::Top,
				  .boxWidth = barW});

		const float radius = std::min(UI::r_pill, 1.5F);
		drawRect({.bounds = {barX, barY, barW, 3.0F},
				  .style = {.fill = UI::bg_inset,
							.border = Foundation::BorderStyle{
								.color = UI::bg_inset, .width = 0.0F, .cornerRadius = radius,
								.position = Foundation::BorderPosition::Inside}}});
		const float fillW = barW * std::clamp(progressFraction, 0.0F, 1.0F);
		if (fillW > 3.0F) {
			drawRect({.bounds = {barX, barY, fillW, 3.0F},
					  .style = {.fill = UI::data,
								.border = Foundation::BorderStyle{
									.color = UI::data, .width = 0.0F, .cornerRadius = radius,
									.position = Foundation::BorderPosition::Inside}}});
		}
	}

	// Viz dock geometry, shared by render and hit-testing.
	UI::SegmentedControl vizControl(const Foundation::Rect& stage) const {
		UI::SegmentedControl::Args args;
		args.options = kVizLabels;
		args.size = UI::Size::Sm;
		args.tone = UI::Tone::Data;
		args.selected = -1;
		for (size_t i = 0; i < kVizModes.size(); ++i) {
			if (kVizModes[i] == globe.colorMode()) args.selected = static_cast<int>(i);
		}
		UI::SegmentedControl probe(args);
		const Foundation::Vec2 fp = probe.footprint();
		args.position = {stage.x + (stage.width - fp.x) * 0.5F, stage.y + stage.height - fp.y - UI::space_4};
		return UI::SegmentedControl(args);
	}

	bool handleVizClick(Foundation::Vec2 p) {
		if (!(model.getState() == world_sim::WorldCreatorState::Reviewing && globe.isReady())) {
			return false;
		}
		const Foundation::Rect stage = stageRect();
		UI::SegmentedControl   control = vizControl(stage);
		const Foundation::Vec2 fp = control.footprint();
		const float x0 = stage.x + (stage.width - fp.x) * 0.5F;
		const float y0 = stage.y + stage.height - fp.y - UI::space_4;
		if (p.x < x0 || p.x > x0 + fp.x || p.y < y0 || p.y > y0 + fp.y) {
			return false;
		}
		const float segW = fp.x / static_cast<float>(kVizModes.size());
		const auto	idx = static_cast<size_t>(std::clamp(
			  static_cast<int>((p.x - x0) / segW), 0, static_cast<int>(kVizModes.size()) - 1));
		globe.setColorMode(kVizModes[idx]);
		return true;
	}

	void renderReviewing(const Foundation::Rect& stage) {
		// Landing marker: pulsing crosshair ring on the selected site.
		if (siteValid) {
			float sx = 0.0F;
			float sy = 0.0F;
			if (globe.projectLatLon(selectedSite, stage, sx, sy)) {
				const float pulse = 0.5F + 0.5F * std::sin(markerPulse * 4.0F);
				const float ringR = 9.0F + pulse * 4.0F;
				Renderer::Primitives::drawCircle({
					.center = {sx, sy},
					.radius = ringR,
					.style = {.fill = Foundation::Color{0.0F, 0.0F, 0.0F, 0.0F},
							  .border = Foundation::BorderStyle{
								  .color = UI::withAlpha(UI::accent, 0.9F - pulse * 0.5F),
								  .width = 1.5F}},
				});
				Renderer::Primitives::drawCircle({
					.center = {sx, sy}, .radius = 2.5F, .style = {.fill = UI::accent}});
				constexpr float kTickIn = 13.0F;
				constexpr float kTickLen = 6.0F;
				const std::array<Foundation::Vec2, 4> dirs{{{1.0F, 0.0F}, {-1.0F, 0.0F}, {0.0F, 1.0F}, {0.0F, -1.0F}}};
				for (const auto& d : dirs) {
					Renderer::Primitives::drawLine({
						.start = {sx + d.x * kTickIn, sy + d.y * kTickIn},
						.end = {sx + d.x * (kTickIn + kTickLen), sy + d.y * (kTickIn + kTickLen)},
						.style = {.color = UI::accent, .width = 1.5F}});
				}
			}
		}

		if (globe.isReady()) {
			vizControl(stage).render();
		}

		if (landingMode) {
			// Crosshair hint, top-center of the stage.
			Renderer::Primitives::drawText({
				.text = "Click the surface to set your descent vector",
				.position = {stage.x, stage.y + UI::space_4},
				.scale = UI::fs_xs / 16.0F,
				.color = UI::text_dim,
				.font = UI::fontMono,
				.hAlign = Foundation::HorizontalAlign::Center,
				.vAlign = Foundation::VerticalAlign::Top,
				.boxWidth = stage.width});

			if (siteValid) {
				detailsPanel.render(details,
									{viewportW - kMarginX - world_sim::LandingSiteDetailsPanel::kWidth,
									 kContentTop, world_sim::LandingSiteDetailsPanel::kWidth,
									 footerTop() - kContentTop - kColumnGap});
			}
		}

		// Pick errors (e.g. clicking water) surface as a short hint at the
		// bottom; selecting a valid tile clears it.
		if (!pickHint.empty()) {
			UI::Text text(UI::Text::Args{
				.position = {stage.x + stage.width * 0.5F, stage.y + stage.height - 60.0F},
				.text = pickHint,
				.style = {
					.color = UI::status_warn,
					.fontSize = UI::fs_md,
					.hAlign = Foundation::HorizontalAlign::Center,
					.vAlign = Foundation::VerticalAlign::Middle,
				},
			});
			text.render();
		}
	}

	void renderFooter() {
		using Renderer::Primitives::drawRect;

		drawRect({.bounds = {kMarginX, footerTop(), viewportW - kMarginX * 2.0F, 1.0F},
				  .style = {.fill = UI::line_hairline}});

		UI::LayoutContainer* footer = activeFooter();
		if (footer == nullptr) return;
		const Foundation::Vec2 want{viewportW, viewportH};
		if (footerLayoutViewport.x != want.x || footerLayoutViewport.y != want.y) {
			footerLayoutViewport = want;
			const Foundation::Rect rect{kMarginX, footerTop() + 16.0F, viewportW - kMarginX * 2.0F, 40.0F};
			surveyFooter->layout(rect);
			landingFooter->layout(rect);
		}
		footer->render();
	}
};

} // namespace

namespace world_sim::scenes {
	extern const world_sim::SceneInfo WorldCreator = {kSceneName, []() {
		return std::make_unique<WorldCreatorScene>();
	}};
}
