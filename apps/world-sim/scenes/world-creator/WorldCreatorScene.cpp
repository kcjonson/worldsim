// World Creator Scene
// Three model states over a shared starfield -- Configuring (empty stage; the
// globe appears only once generated), Generating (progress strip), Reviewing
// (final globe) -- in a 360 | stage | 360 row. The parameter sidebar carries
// a full-width Generate/Cancel button at its bottom; the right column stacks
// the World Survey pane and, once a site is picked, the Landing Zone pane.
// Landing selection lives in Reviewing: pick on the globe, then Confirm
// Landing Site opens a Commit to Descent dialog gating the actual landing.

#include "GameStartConfig.h"
#include "NewGameSetup.h"
#include "SceneTypes.h"
#include "WorldCreatorModel.h"
#include "scenes/landing/LandingSiteDetailsModel.h"
#include "scenes/landing/LandingSiteDetailsPanel.h"
#include "scenes/shared/GlobeView.h"
#include "scenes/shared/SalalUi.h"
#include "scenes/shared/Starfield.h"
#include "ui/ParameterPanel.h"
#include "ui/WorldSurveyPanel.h"

#include <worldgen/data/WorldData.h>
#include <worldgen/io/PlanetIO.h>
#include <worldgen/sampling/LandingSite.h>

#include <app/Keys.h>
#include <application/App.h>
#include <graphics/Color.h>
#include <primitives/Primitives.h>
#include <theme/Panel.h>
#include <ui/components/Button.h>
#include <ui/components/Dialog.h>
#include <ui/components/ProgressBar.h>
#include <ui/components/SegmentedControl.h>
#include <ui/shapes/Rectangle.h>
#include <ui/shapes/Text.h>
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
// Right column holds the World Survey pane and, once a site is picked, the
// Landing Zone pane below it. Sized to the wider of the two panels.
constexpr float kRightColW = 360.0F;
// Keyboard orbit speed (radians/sec) for arrow keys and WASD.
constexpr float kKeyPanRate = 1.2F;
// Creator opens at preview resolution for fast iteration; must match the
// resolution select's initial value in ParameterPanel (PlanetParams defaults
// to 1024, which the panel would otherwise misreport).
constexpr uint32_t kInitialSubdivision = 256;

// Viz switcher options and their color modes (right-click cycling can still
// reach the other analyst modes; the dock then shows no active segment).
constexpr std::array<planetview::ColorMode, 4> kVizModes{
	planetview::ColorMode::Terrain,
	planetview::ColorMode::Biome,
	planetview::ColorMode::Temperature,
	planetview::ColorMode::Precipitation
};

namespace tokens = world_sim::tokens;
namespace family = ui::tokens::font::family;

// Shows or hides a component when that changes.
void show(ui::Component* component, bool visible) {
	if (component != nullptr && component->isVisible() != visible) {
		component->setVisible(visible);
	}
}

void enable(ui::Component* component, bool enabled) {
	if (component != nullptr && component->isEnabled() != enabled) {
		component->setEnabled(enabled);
	}
}

class WorldCreatorScene : public app::Scene {
  public:
	const char* name() const override { return kSceneName; }
	std::string exportState() const override {
		return std::format(
			R"({{"scene":"world_creator","state":{},"globe":{},"site":{},"confirming":{}}})",
			static_cast<int>(model.getState()),
			globe.isReady() ? "true" : "false",
			siteValid ? "true" : "false",
			confirmDialog.get() != nullptr ? "true" : "false"
		);
	}

	void onEnter(app::App& app) override {
		owner = &app;
		LOG_INFO(Game, "WorldCreatorScene - Entering");
		world_sim::enterSalalUi(app, ui());

		build();

		// Pre-fill a random seed so the field isn't blank on entry.
		// Uses the same mechanism as the Random button.
		model.randomizeSeed();
		syncPanelFromModel();
		refreshFooter();
	}

	void onExit() override { LOG_INFO(Game, "WorldCreatorScene - Exiting"); }

	void update(const app::FrameContext& frame) override {
		const auto dt = static_cast<float>(frame.dt);

		// A dialog closed by this frame's Escape mustn't also back the scene out.
		const bool dialogEscape = confirmDialog.get() != nullptr || dialogClosed;
		dialogClosed = false;
		if (engine::key(frame.input, foundation::Key::Escape).pressed() && !dialogEscape) {
			// ESC backs out: Configuring returns to the crew step of the New
			// Game flow; Generating/Reviewing keep bailing to the main menu.
			if (model.getState() == world_sim::WorldCreatorState::Configuring) {
				LOG_INFO(Game, "WorldCreatorScene: returning to party select");
				owner->scenes().switchTo(world_sim::scenes::PartySelect.name);
				return;
			}
			if (model.getState() == world_sim::WorldCreatorState::Generating) {
				model.cancelGeneration();
			}
			LOG_INFO(Game, "WorldCreatorScene: returning to main menu");
			owner->scenes().switchTo(world_sim::scenes::MainMenu.name);
			return;
		}

		globe.update(dt);
		markerPulse += dt;

		auto state = model.getState();

		if (state == world_sim::WorldCreatorState::Generating) {
			auto prog = model.pollProgress();
			showProgress(prog.totalFraction, prog.stageName != nullptr ? prog.stageName : "");

			// Re-check after poll (state may have changed)
			auto newState = model.getState();
			if (newState != state) {
				// Anything other than a clean completion or a user cancel is an error
				if (newState == world_sim::WorldCreatorState::Configuring &&
				    prog.state != worldgen::GenerationProgress::State::Cancelled) {
					setError("World generation failed. Adjust parameters and try again.");
					LOG_ERROR(Game, "WorldCreatorScene: generation ended without a result (state=%d)",
					          static_cast<int>(prog.state));
				}
				onStateChanged(newState);
			}
		} else if (state == world_sim::WorldCreatorState::Reviewing && confirmDialog.get() == nullptr) {
			handleCameraKeys(frame.input, dt);
		}

		enable(generateButton.get(), panel.seedIsValid());
		enable(acceptButton.get(), siteValid); // stays in sync as the player re-picks a site

		const bool ready = model.getState() == world_sim::WorldCreatorState::Reviewing && globe.isReady();
		show(stageHint.get(), ready);
		show(vizDock.get(), ready);
		// Right-click cycling reaches modes the dock hasn't got; it then shows none.
		if (ui::SegmentedControl* viz = vizControl.get(); viz != nullptr && viz->getSelected() != vizIndex()) {
			viz->setSelected(vizIndex());
		}
	}

	void render(float /*alpha*/) override {
		const ui::Theme& theme = ui().getTheme();
		Renderer::Primitives::clear(theme.get(tokens::color::bgVoid));

		int vpW = 0;
		int vpH = 0;
		Renderer::Primitives::getLogicalViewport(vpW, vpH);
		const auto viewportW = static_cast<float>(vpW);
		const auto viewportH = static_cast<float>(vpH);

		world_sim::renderStarfield(theme, vpW, vpH, 5U, true);

		// No planet is shown until generation completes: the stage stays empty
		// while configuring, shows only the progress strip while generating, and
		// reveals the real globe once it is ready. Never a stand-in world.
		if (model.getState() != world_sim::WorldCreatorState::Reviewing || !globe.isReady()) {
			return;
		}

		// 3D pass first: Primitives batches flush after the scene, so the ui
		// composites on top of the blitted globe. The explicit flush paints
		// the starfield beneath the blit.
		const Foundation::Rect rect = stageRect();
		Renderer::Primitives::flush();
		globe.render(rect, viewportW, viewportH);
		renderMarker(rect, theme);
	}

  private:
	app::App* owner = nullptr;

	world_sim::WorldCreatorModel model;

	world_sim::ParameterPanel		   panel{callbacks()};
	world_sim::WorldSurveyPanel		   surveyPanel;
	world_sim::LandingSiteDetailsPanel detailsPanel;

	ui::Ref<ui::Component>		  stage;
	ui::Ref<ui::Component>		  landingHost;
	ui::Ref<ui::Button>			  generateButton; // bottom of the sidebar
	ui::Ref<ui::Button>			  cancelButton;	  // bottom of the sidebar
	ui::Ref<ui::Text>			  statusText;
	ui::Ref<ui::Button>			  regenerateButton;
	ui::Ref<ui::Button>			  acceptButton;
	ui::Ref<ui::Text>			  escHint;
	ui::Ref<ui::Text>			  stageHint;
	ui::Ref<ui::Text>			  pickHintText;
	ui::Ref<ui::Text>			  errorText;
	ui::Ref<ui::Component>		  vizDock;
	ui::Ref<ui::SegmentedControl> vizControl;
	ui::Ref<ui::Component>		  progressStrip;
	ui::Ref<ui::Text>			  progressStage;
	ui::Ref<ui::Text>			  progressPercent;
	ui::Ref<ui::ProgressBar>	  progressBar;
	ui::Ref<ui::Dialog>			  confirmDialog;
	bool						  dialogClosed = false;

	world_sim::GlobeView globe;
	float				 markerPulse{0.0F};

	// Landing selection (Reviewing only). The site auto-suggests on completion
	// and updates on each land-tile click.
	planetview::LatLon			  selectedSite{};
	bool						  siteValid{false};
	world_sim::LandingSiteDetails details;

	// Stage (globe area) between the parameter sidebar and the right column, as the last layout put it.
	Foundation::Rect stageRect() const {
		const ui::Component* shown = stage.get();
		return shown != nullptr ? shown->screenBounds() : Foundation::Rect{};
	}

	world_sim::ParameterPanelCallbacks callbacks() {
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
		return cbs;
	}

	void build() {
		const ui::Theme& theme = ui().getTheme();
		const float		 space3 = theme.get(tokens::spacing::space3);
		const float		 space4 = theme.get(tokens::spacing::space4);
		const ui::Style	 mono = world_sim::text(
			 theme,
			 {.family = family::mono,
			  .size = tokens::fontSize::fsXs,
			  .color = tokens::color::textDim,
			  .tracking = tokens::letterSpacing::lsWide}
		 );

		ui::Ref<ui::Component> screen = root().addChild<ui::Component>({
			.id = "world_creator",
			.size = {ui::percent(100.0F), ui::percent(100.0F)},
			.layout = {
				.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .padding = {0.0F, kMarginX, 0.0F, kMarginX}
			},
		});

		// Header: kicker over the title, the ESC hint at the right.
		ui::Ref<ui::Component> header = screen.addChild<ui::Component>({
			.size = {ui::percent(100.0F), kContentTop},
			.layout = {
				.display = ui::Display::Flex,
				.padding = {20.0F, 0.0F, 0.0F, 0.0F},
				.alignItems = ui::AlignItems::Start,
				.justifyContent = ui::JustifyContent::SpaceBetween
			},
		});
		ui::Ref<ui::Component> titles = header.addChild<ui::Component>({
			.layout = {.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .gap = 6.0F},
		});
		titles.addChild<ui::Text>({
			.id = "wc_kicker",
			.style = world_sim::text(
				theme,
				{.family = family::mono,
				 .size = tokens::fontSize::fs2xs,
				 .color = tokens::color::textFaint,
				 .tracking = tokens::letterSpacing::lsWider}
			),
			.text = "// NEW GAME    STEP 03 / 03",
		});
		titles.addChild<ui::Text>({
			.id = "wc_title",
			.style = world_sim::text(
				theme,
				{.family = family::display,
				 .size = tokens::fontSize::fs3xl,
				 .color = tokens::color::textBright,
				 .tracking = tokens::letterSpacing::lsWide}
			),
			.text = "Generate Planet",
		});
		escHint = header.addChild<ui::Text>({
			.id = "wc_esc_hint",
			.style = world_sim::text(theme, {.size = tokens::fontSize::fsBase, .color = tokens::color::textDim}),
			.text = "ESC: Back to crew",
		});

		// The three columns.
		ui::Ref<ui::Component> columns = screen.addChild<ui::Component>({
			.size = {ui::percent(100.0F), 0.0F},
			.margin = {0.0F, 0.0F, kColumnGap, 0.0F},
			.layout = {.display = ui::Display::Flex, .gap = kColumnGap, .flexGrow = 1.0F},
		});

		ui::Ref<::theme::Panel> sidebar = columns.addChild<::theme::Panel>({
			.id = "wc_parameters",
			.size = {world_sim::ParameterPanel::kPanelWidth, ui::kAuto},
			.title = "Parameters",
			.kicker = "Survey Config",
			.accent = ::theme::PanelAccent::Data,
		});
		if (const ::theme::Panel* shown = sidebar.get()) {
			ui::Ref<ui::Component> body = shown->body();
			model.setGridSubdivision(kInitialSubdivision);
			panel.build(body, theme);
			// Generate / Cancel pinned full-width to the bottom of the sidebar.
			generateButton = body.addChild<ui::Button>({
				.id = "btn_wc_generate",
				.size = {ui::percent(100.0F), 40.0F},
				.margin = {space3, 0.0F, 0.0F, 0.0F},
				.label = "Generate",
				.icon = "globe",
				.variant = ui::ButtonVariant::Primary,
				.onPress = [this] { startGeneration(); },
			});
			cancelButton = body.addChild<ui::Button>({
				.id = "btn_wc_cancel",
				.visible = false,
				.size = {ui::percent(100.0F), 40.0F},
				.margin = {space3, 0.0F, 0.0F, 0.0F},
				.label = "Cancel",
				.onPress = [this] { model.cancelGeneration(); },
			});
		}

		stage = columns.addChild<ui::Component>({
			.id = "wc_stage",
			.size = {0.0F, ui::percent(100.0F)},
			.layout = {.flexGrow = 1.0F},
			.pointerEvents = ui::PointerEvents::Auto,
			.onPointerDown = [this](ui::PointerEvent& event) { stagePress(event); },
			.onPointerUp = [this](ui::PointerEvent& event) { globe.handleInput(event, stageRect(), true); },
			.onPointerMove =
				[this](ui::PointerEvent& event) {
					if (globe.isDragging()) {
						globe.handleInput(event, stageRect(), true);
					}
				},
		});
		if (ui::Component* shownStage = stage.get()) {
			shownStage->setOnPointer(ui::PointerEvent::Type::Wheel, [this](ui::PointerEvent& event) {
				if (reviewing()) {
					globe.handleInput(event, stageRect(), true);
				}
			});
			shownStage->setOnPointer(ui::PointerEvent::Type::Cancel, [this](ui::PointerEvent& event) {
				globe.handleInput(event, stageRect(), true);
			});
		}
		buildStage(theme, mono);

		ui::Ref<ui::Component> right = columns.addChild<ui::Component>({
			.id = "wc_right",
			.size = {kRightColW, ui::kAuto},
			.layout = {.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .gap = kColumnGap},
		});
		surveyPanel.mount(
			right.addChild<ui::Component>(
				{.id = "wc_survey", .layout = {.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column}}
			),
			theme
		);
		landingHost = right.addChild<ui::Component>({
			.id = "wc_landing",
			.visible = false,
			.size = {ui::percent(100.0F), 0.0F},
			.layout = {.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .flexGrow = 1.0F},
		});
		detailsPanel.mount(landingHost, theme);

		// Footer: a hairline, then Back | status | actions.
		ui::Ref<ui::Component> footer = screen.addChild<ui::Component>({
			.size = {ui::percent(100.0F), kFooterHeight},
			.layout = {.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .gap = 15.0F},
		});
		footer.addChild<ui::Rectangle>(
			{.size = {ui::percent(100.0F), 1.0F}, .style = {.backgroundColor = theme.get(tokens::color::lineHairline)}}
		);
		ui::Ref<ui::Component> bar = footer.addChild<ui::Component>({
			.id = "wc_footer",
			.size = {ui::percent(100.0F), 40.0F},
			.layout = {.display = ui::Display::Flex, .gap = space4, .alignItems = ui::AlignItems::Center},
		});
		bar.addChild<ui::Button>({
			.id = "btn_wc_back",
			.size = {120.0F, 40.0F},
			.label = "Back",
			.icon = "chevronLeft",
			.onPress = [this] { goBackToParty(); },
		});
		ui::Style status = mono;
		status.textAlign = Foundation::HorizontalAlign::Center;
		statusText = bar.addChild<ui::Text>({
			.id = "wc_status",
			.size = {0.0F, ui::kAuto},
			.layout = {.flexGrow = 1.0F},
			.style = status,
			.text = "Awaiting generation",
		});
		ui::Ref<ui::Component> actions = bar.addChild<ui::Component>({
			.id = "wc_footer_actions",
			.layout = {.display = ui::Display::Flex, .gap = space3, .alignItems = ui::AlignItems::Center},
		});
		regenerateButton = actions.addChild<ui::Button>({
			.id = "btn_wc_regenerate",
			.visible = false,
			.size = {150.0F, 40.0F},
			.label = "Regenerate",
			.icon = "refresh",
			.variant = ui::ButtonVariant::Ghost,
			.onPress = [this] { startGeneration(); },
		});
		acceptButton = actions.addChild<ui::Button>({
			.id = "btn_wc_accept",
			.visible = false,
			.size = {230.0F, 40.0F},
			.label = "Confirm Landing Site",
			.icon = "rocket",
			.variant = ui::ButtonVariant::Primary,
			.onPress = [this] { openConfirmDialog(); },
		});

		surveyPanel.setWorld(nullptr);
		if (ui::Button* generate = generateButton.get()) {
			generate->focus();
		}
	}

	// What sits over the globe: the crosshair hint at the top, the viz dock, the pick hint and the
	// progress strip at the foot, and a failed generation's error.
	void buildStage(const ui::Theme& theme, const ui::Style& mono) {
		const float space4 = theme.get(tokens::spacing::space4);
		ui::Style	hint = mono;
		hint.textAlign = Foundation::HorizontalAlign::Center;
		stageHint = stage.addChild<ui::Text>({
			.id = "wc_stage_hint",
			.visible = false,
			.layout = {.position = ui::Position::Absolute, .inset = {.top = space4, .right = 0.0F, .left = 0.0F}},
			.style = hint,
			.text = "Click the surface to set your descent vector",
		});

		ui::Style pick = world_sim::text(
			theme, {.size = tokens::fontSize::fsMd, .color = tokens::color::statusWarn, .align = Foundation::HorizontalAlign::Center}
		);
		pickHintText = stage.addChild<ui::Text>({
			.id = "wc_pick_hint",
			.visible = false,
			.layout = {.position = ui::Position::Absolute, .inset = {.right = 0.0F, .bottom = 60.0F, .left = 0.0F}},
			.style = pick,
			.text = "Select a land tile to start the colony",
		});

		vizDock = stage.addChild<ui::Component>({
			.id = "wc_viz_dock",
			.visible = false,
			.layout = {
				.display = ui::Display::Flex,
				.justifyContent = ui::JustifyContent::Center,
				.position = ui::Position::Absolute,
				.inset = {.right = 0.0F, .bottom = space4, .left = 0.0F}
			},
		});
		vizControl = vizDock.addChild<ui::SegmentedControl>({
			.id = "wc_viz",
			.options = {"Terrain", "Biomes", "Temp", "Rain"},
			.selected = 0,
			.tone = ui::Tone::Info,
			.controlSize = ui::ControlSize::Sm,
			.onChange = [this](int index) {
				if (index >= 0 && static_cast<size_t>(index) < kVizModes.size()) {
					globe.setColorMode(kVizModes[static_cast<size_t>(index)]);
				}
			},
		});

		// Centered progress strip: teal stage label left, amber percent right.
		progressStrip = stage.addChild<ui::Component>({
			.id = "wc_progress",
			.visible = false,
			.layout = {
				.display = ui::Display::Flex,
				.flexDirection = ui::FlexDirection::Column,
				.gap = 6.0F,
				.alignItems = ui::AlignItems::Center,
				.position = ui::Position::Absolute,
				.inset = {.right = 0.0F, .bottom = theme.get(tokens::spacing::space12) - 3.0F, .left = 0.0F}
			},
		});
		ui::Ref<ui::Component> labels = progressStrip.addChild<ui::Component>({
			.size = {420.0F, ui::kAuto},
			.layout = {.display = ui::Display::Flex, .justifyContent = ui::JustifyContent::SpaceBetween},
		});
		ui::Style			   stageStyle = mono;
		stageStyle.color = theme.get(tokens::color::dataBright);
		progressStage = labels.addChild<ui::Text>({.id = "wc_progress_stage", .style = stageStyle, .text = "Starting..."});
		ui::Style percent = mono;
		percent.color = theme.get(tokens::color::accentBright);
		progressPercent = labels.addChild<ui::Text>({.id = "wc_progress_percent", .style = percent, .text = "0%"});
		progressBar = progressStrip.addChild<ui::ProgressBar>(
			{.id = "wc_progress_bar",
			 .size = {420.0F, ui::kAuto},
			 .value = 0.0F,
			 .tone = ui::Tone::Info,
			 .controlSize = ui::ControlSize::Sm}
		);

		errorText = stage.addChild<ui::Text>({
			.id = "wc_error",
			.visible = false,
			.layout = {.position = ui::Position::Absolute, .inset = {.bottom = 12.0F, .left = 0.0F}},
			.style = world_sim::text(theme, {.size = tokens::fontSize::fsMd, .color = tokens::color::statusCrit}),
		});
	}

	bool reviewing() const { return model.getState() == world_sim::WorldCreatorState::Reviewing && globe.isReady(); }

	int vizIndex() const {
		for (size_t i = 0; i < kVizModes.size(); ++i) {
			if (kVizModes[i] == globe.colorMode())
				return static_cast<int>(i);
		}
		return -1;
	}

	// Pick before orbit-drag so a left click selects the tile under the cursor; the same press then
	// begins an orbit drag, the stage holding the pointer until the release.
	void stagePress(ui::PointerEvent& event) {
		if (!reviewing()) {
			return;
		}
		const Foundation::Rect rect = stageRect();
		if (event.button == foundation::MouseButton::Left) {
			if (auto picked = globe.pick(event.screen, rect)) {
				trySelectSite(*picked);
			}
		}
		if (globe.handleInput(event, rect, true) && globe.isDragging()) {
			if (ui::Component* shown = stage.get()) {
				shown->capturePointer();
			}
		}
	}

	void setError(const std::string& message) {
		if (ui::Text* error = errorText.get()) {
			error->setText(message);
		}
	}

	void showProgress(float fraction, const std::string& stageName) {
		if (ui::Text* label = progressStage.get(); label != nullptr && label->getText() != stageName) {
			label->setText(stageName);
		}
		if (ui::Text* percent = progressPercent.get()) {
			percent->setText(std::format("{}%", static_cast<int>(fraction * 100.0F)));
		}
		if (ui::ProgressBar* bar = progressBar.get()) {
			bar->setValue(std::clamp(fraction, 0.0F, 1.0F));
		}
	}

	void setPickHint(bool shown) { show(pickHintText.get(), shown); }

	// Show/hide footer actions for the current model state.
	void refreshFooter() {
		const auto state = model.getState();
		const bool configuring = state == world_sim::WorldCreatorState::Configuring;
		const bool generating = state == world_sim::WorldCreatorState::Generating;
		const bool review = state == world_sim::WorldCreatorState::Reviewing;
		show(generateButton.get(), configuring);
		show(cancelButton.get(), generating);
		show(regenerateButton.get(), review);
		show(acceptButton.get(), review);
		show(progressStrip.get(), generating);
		show(landingHost.get(), review && siteValid);
		if (ui::Text* error = errorText.get()) {
			show(error, configuring && !error->getText().empty());
		}
		if (ui::Text* hint = escHint.get()) {
			hint->setText(configuring ? "ESC: Back to crew" : "ESC: Back to menu");
		}

		if (ui::Text* status = statusText.get()) {
			const ui::Theme& theme = ui().getTheme();
			ui::Style		 style = status->getDeclaredStyle();
			style.color = theme.get(review ? tokens::color::statusOk : tokens::color::textDim);
			status->setStyle(style);
			switch (state) {
				case world_sim::WorldCreatorState::Configuring:
					status->setText("Awaiting generation");
					break;
				case world_sim::WorldCreatorState::Generating:
					status->setText("Generating world...");
					break;
				case world_sim::WorldCreatorState::Reviewing:
					status->setText("World ready - suitable for colonization");
					break;
			}
		}
	}

	void syncPanelFromModel() {
		const auto& p = model.getParams();
		panel.syncValues(
			p.waterAmount * 100.0, // fraction -> percent for UI
			p.tectonicPlateCount,
			p.planetRadius,
			p.rotationRate,
			p.planetAge,
			p.atmosphereStrength,
			p.starTemperature,
			p.semiMajorAxis,
			p.eccentricity,
			p.seed
		);
	}

	void startGeneration() {
		if (!panel.seedIsValid()) {
			return;
		}

		// Blank seed means "surprise me": pick a fresh seed each run so regenerating
		// doesn't silently reproduce the identical world from the preset default.
		if (panel.seedIsEmpty()) {
			model.randomizeSeed();
			syncPanelFromModel();
		}

		setError("");
		siteValid = false;
		setPickHint(false);
		showProgress(0.0F, "Starting...");

		model.startGeneration();
		panel.setGenerating(true);
		surveyPanel.setWorld(nullptr);
		refreshFooter();
		if (ui::Button* cancel = cancelButton.get()) {
			cancel->focus();
		}
	}

	void onStateChanged(world_sim::WorldCreatorState newState) {
		if (newState == world_sim::WorldCreatorState::Reviewing) {
			panel.setGenerating(false);

			if (auto result = model.getResult()) {
				globe.setWorld(result);
				surveyPanel.setWorld(result);
				// Suggest a habitable starting point so the pane has content the
				// moment review begins; the player can click to choose another.
				auto suggested = worldgen::findDefaultLandingSite(*result);
				selectedSite = {static_cast<float>(suggested.latDeg),
				                static_cast<float>(suggested.lonDeg)};
				siteValid = true;
				setPickHint(false);
				refreshDetails();
			}
			refreshFooter();
			if (ui::Button* accept = acceptButton.get()) {
				accept->focus();
			}
			return;
		}
		if (newState == world_sim::WorldCreatorState::Configuring) {
			panel.setGenerating(false);
			siteValid = false;
		}
		refreshFooter();
		if (ui::Button* generate = generateButton.get()) {
			generate->focus();
		}
	}

	void openConfirmDialog() {
		ui::Component* opener = acceptButton.get();
		if (!siteValid || opener == nullptr || confirmDialog.get() != nullptr)
			return;
		confirmDialog = ui::Dialog::open(
			*opener,
			{.title = "Commit to Descent", .kicker = "Confirm", .panelSize = {440.0F, ui::kAuto}, .footer = true, .onClose = [this] {
				 dialogClosed = true;
			 }}
		);
		const ui::Dialog* dialog = confirmDialog.get();
		if (dialog == nullptr) {
			return;
		}
		dialog->content().addChild<ui::Text>({
			.id = "confirm_body",
			.size = {ui::percent(100.0F), ui::kAuto},
			.style = world_sim::text(ui().getTheme(), {.size = tokens::fontSize::fsMd}),
			.text = std::format(
				"Your colony will begin at {} - {}. Once you commit, there is no turning back.",
				details.coords,
				details.biomeName.empty() ? "unknown terrain" : details.biomeName
			),
			.wrap = true,
		});
		ui::Ref<ui::Component> actions = dialog->footer();
		actions.addChild<ui::Button>({
			.id = "btn_descent_cancel",
			.label = "Cancel",
			.onPress = [this] {
				if (ui::Dialog* shown = confirmDialog.get())
					shown->close();
			},
		});
		const ui::Ref<ui::Button> descend = actions.addChild<ui::Button>({
			.id = "btn_begin_descent",
			.label = "Begin Descent",
			.icon = "rocket",
			.variant = ui::ButtonVariant::Primary,
			.onPress = [this] {
				if (ui::Dialog* shown = confirmDialog.get())
					shown->close();
				land();
			},
		});
		if (ui::Button* shown = descend.get()) {
			shown->focus();
		}
	}

	void handleCameraKeys(const foundation::InputSnapshot& input, float dt) {
		// While the seed field is focused, keys belong to it (text editing,
		// cursor arrows), not the camera.
		if (panel.isSeedFocused())
			return;

		const auto down = [&input](foundation::Key key) {
			return engine::key(input, key).down;
		};
		const float step = kKeyPanRate * dt;
		float dYaw = 0.0F;
		float dPitch = 0.0F;
		if (down(foundation::Key::Left) || down(foundation::Key::A))
			dYaw += step;
		if (down(foundation::Key::Right) || down(foundation::Key::D))
			dYaw -= step;
		if (down(foundation::Key::Up) || down(foundation::Key::W))
			dPitch += step;
		if (down(foundation::Key::Down) || down(foundation::Key::S))
			dPitch -= step;
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

		// Pick errors (clicking water) surface as a short hint at the bottom;
		// selecting a valid tile clears it.
		if (isWaterTile(*result, tile)) {
			setPickHint(true);
			return;
		}

		selectedSite = picked;
		siteValid = true;
		setPickHint(false);
		refreshDetails();
		refreshFooter();
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
			detailsPanel.show(details);
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
		owner->scenes().switchTo(world_sim::scenes::GameLoading.name);
	}

	void goBackToParty() {
		if (model.getState() == world_sim::WorldCreatorState::Generating) {
			model.cancelGeneration();
		}
		owner->scenes().switchTo(world_sim::scenes::PartySelect.name);
	}

	// Landing marker: pulsing crosshair ring on the selected site.
	void renderMarker(const Foundation::Rect& rect, const ui::Theme& theme) const {
		float sx = 0.0F;
		float sy = 0.0F;
		if (!siteValid || !globe.projectLatLon(selectedSite, rect, sx, sy)) {
			return;
		}
		const Foundation::Color accent = theme.get(tokens::color::accent);
		Foundation::Color		ring = accent;
		const float				pulse = 0.5F + 0.5F * std::sin(markerPulse * 4.0F);
		ring.a = 0.9F - pulse * 0.5F;
		const float ringR = 9.0F + pulse * 4.0F;
		Renderer::Primitives::drawCircle({
			.center = {sx, sy},
			.radius = ringR,
			.style = {.fill = Foundation::Color{0.0F, 0.0F, 0.0F, 0.0F}, .border = Foundation::BorderStyle{.color = ring, .width = 1.5F}},
		});
		Renderer::Primitives::drawCircle({.center = {sx, sy}, .radius = 2.5F, .style = {.fill = accent}});
		constexpr float						  kTickIn = 13.0F;
		constexpr float						  kTickLen = 6.0F;
		const std::array<Foundation::Vec2, 4> dirs{{{1.0F, 0.0F}, {-1.0F, 0.0F}, {0.0F, 1.0F}, {0.0F, -1.0F}}};
		for (const auto& d : dirs) {
			Renderer::Primitives::drawLine(
				{.start = {sx + d.x * kTickIn, sy + d.y * kTickIn},
				 .end = {sx + d.x * (kTickIn + kTickLen), sy + d.y * (kTickIn + kTickLen)},
				 .style = {.color = accent, .width = 1.5F}}
			);
		}
	}
};

} // namespace

namespace world_sim::scenes {
	extern const world_sim::SceneInfo WorldCreator = {kSceneName, []() {
		return std::make_unique<WorldCreatorScene>();
	}};
}
