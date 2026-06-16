#include "ParameterPanel.h"

#include "primitives/Primitives.h"

#include <focus/FocusManager.h>

#include <algorithm>
#include <cctype>
#include <format>
#include <string>
#include <vector>

namespace world_sim {

namespace {
	// Common slider width inside the panel
	constexpr float kSliderWidth = 300.0F;
	constexpr float kSliderX = 10.0F; // offset from panel left
}

ParameterPanel::ParameterPanel(Foundation::Vec2 pos, ParameterPanelCallbacks cbs)
	: position(pos), callbacks(std::move(cbs)) {
	buildWidgets();
}

UI::Text ParameterPanel::makeLabel(const std::string& text, float y) {
	return UI::Text(UI::Text::Args{
		.position = {position.x + kSliderX, y},
		.text = text,
		.style = {
			.color = Foundation::Color{0.65F, 0.65F, 0.65F, 1.0F},
			.fontSize = 11.0F,
			.hAlign = Foundation::HorizontalAlign::Left,
			.vAlign = Foundation::VerticalAlign::Top,
		},
	});
}

float ParameterPanel::addLabel(const std::string& text) {
	float y = position.y + nextY;
	sectionLabels.push_back(makeLabel(text, y));
	nextY += kLabelHeight + 2.0F;
	return y;
}

float ParameterPanel::addSlider(
	std::unique_ptr<UI::Slider>& out,
	double min, double max, double step, double value,
	bool logScale, const std::string& label,
	std::function<std::string(double)> formatter,
	std::function<void(double)> onChange)
{
	float y = position.y + nextY;
	out = std::make_unique<UI::Slider>(UI::Slider::Args{
		.position = {position.x + kSliderX, y},
		.size = {kSliderWidth, kSliderHeight},
		.min = min,
		.max = max,
		.step = step,
		.value = value,
		.logScale = logScale,
		.label = label,
		.valueFormatter = std::move(formatter),
		.onChanged = std::move(onChange),
	});
	nextY += kSliderHeight + kItemSpacing;
	return y;
}

void ParameterPanel::buildWidgets() {
	nextY = 8.0F;

	// Preset selector
	addLabel("Preset");
	float presetY = position.y + nextY;
	std::vector<UI::SelectOption> presets{
		{"Earth-Like",    "earth_like"},
		{"Desert World",  "desert_world"},
		{"Ocean World",   "ocean_world"},
		{"Frozen World",  "frozen_world"},
		{"Volcanic World","volcanic_world"},
		{"Ancient Garden","ancient_garden"},
	};
	presetSelect = std::make_unique<UI::Select>(UI::Select::Args{
		.position = {position.x + kSliderX, presetY},
		.size = {kSliderWidth, 30.0F},
		.options = presets,
		.value = "earth_like",
		.onChange = callbacks.onPresetChanged,
		.id = "preset_select",
	});
	nextY += 30.0F + kSectionSpacing;

	// Planet Properties
	addLabel("-- Planet Properties --");
	addSlider(waterSlider, 0.0, 100.0, 1.0, 70.0, false,
		"Water %",
		[](double v) { return std::format("{:.0f}%", v); },
		callbacks.onWaterAmount);

	addSlider(platesSlider, 2.0, 30.0, 1.0, 12.0, false,
		"Tectonic Plates",
		[](double v) { return std::format("{:.0f}", v); },
		[this](double v) { if (callbacks.onTectonicPlates) callbacks.onTectonicPlates(v); });

	addSlider(radiusSlider, 0.1, 10.0, 0.0, 1.0, true,
		"Radius (Re)",
		[](double v) { return std::format("{:.2f}", v); },
		callbacks.onPlanetRadius);

	addSlider(rotationSlider, 0.1, 100.0, 0.0, 1.0, true,
		"Rotation (d)",
		[](double v) { return std::format("{:.2f}", v); },
		callbacks.onRotationRate);

	addSlider(ageSlider, 1.0e7, 1.0e10, 0.0, 4.5e9, true,
		"Planet Age (yr)",
		[](double v) {
			if (v >= 1.0e9) return std::format("{:.1f}B yr", v / 1.0e9);
			return std::format("{:.0f}M yr", v / 1.0e6);
		},
		callbacks.onPlanetAge);

	addSlider(atmosphereSlider, 0.1, 10.0, 0.0, 1.0, true,
		"Atmosphere",
		[](double v) { return std::format("{:.2f} atm", v); },
		callbacks.onAtmosphere);

	nextY += kSectionSpacing;

	// Star / Orbital
	addLabel("-- Star & Orbit --");
	addSlider(starTempSlider, 2000.0, 50000.0, 0.0, 5778.0, true,
		"Star Temp (K)",
		[](double v) { return std::format("{:.0f}K", v); },
		callbacks.onStarTemperature);

	addSlider(semiMajorSlider, 0.1, 100.0, 0.0, 1.0, true,
		"Semi-Major (AU)",
		[](double v) { return std::format("{:.2f} AU", v); },
		callbacks.onSemiMajorAxis);

	addSlider(eccentricitySlider, 0.0, 0.95, 0.0, 0.017, false,
		"Eccentricity",
		[](double v) { return std::format("{:.3f}", v); },
		callbacks.onEccentricity);

	nextY += kSectionSpacing;

	// Generator settings
	addLabel("-- Generator --");
	float resY = position.y + nextY;
	std::vector<UI::SelectOption> resOptions{
		{"Preview 256",  "256"},
		{"Low 512",      "512"},
		{"Default 1024", "1024"},
		{"High 1449",    "1449"},
		{"Ultra 2048",   "2048"},
	};
	resolutionSelect = std::make_unique<UI::Select>(UI::Select::Args{
		.position = {position.x + kSliderX, resY},
		.size = {kSliderWidth, 30.0F},
		.options = resOptions,
		.value = "256",
		.onChange = callbacks.onResolutionChanged,
		.id = "resolution_select",
	});
	nextY += 30.0F + kItemSpacing;

	// Seed row
	float seedY = position.y + nextY;
	seedInput = std::make_unique<UI::TextInput>(UI::TextInput::Args{
		.position = {position.x + kSliderX, seedY},
		.size = {200.0F, 28.0F},
		.placeholder = "Random seed...",
		.id = "seed_input",
		.onChange = [this](const std::string& text) { onSeedTextChanged(text); },
	});

	randomizeButton = std::make_unique<UI::Button>(UI::Button::Args{
		.label = "Random",
		.position = {position.x + kSliderX + 208.0F, seedY},
		.size = {90.0F, 28.0F},
		.type = UI::Button::Type::Secondary,
		.onClick = callbacks.onRandomize,
		.id = "btn_randomize",
	});
	nextY += 28.0F + 2.0F;
	seedErrorY = position.y + nextY;
	nextY += kLabelHeight + kSectionSpacing - 2.0F;

	// Action buttons
	float btnY = position.y + nextY;
	generateButton = std::make_unique<UI::Button>(UI::Button::Args{
		.label = "Generate",
		.position = {position.x + kSliderX, btnY},
		.size = {kSliderWidth, 36.0F},
		.type = UI::Button::Type::Primary,
		.onClick = callbacks.onGenerate,
		.id = "btn_generate",
	});

	cancelButton = std::make_unique<UI::Button>(UI::Button::Args{
		.label = "Cancel",
		.position = {position.x + kSliderX, btnY},
		.size = {kSliderWidth, 36.0F},
		.type = UI::Button::Type::Secondary,
		.disabled = false,
		.onClick = callbacks.onCancel,
		.id = "btn_cancel",
	});
	cancelButton->visible = false;
}

void ParameterPanel::setGenerating(bool gen) {
	generating = gen;

	// Toggle which button is visible
	if (generateButton) { generateButton->visible = !gen; applyGenerateEnabled(); }
	if (cancelButton)   { cancelButton->visible = gen; }

	// Disable all parameter controls during generation
	bool dis = gen;
	if (presetSelect)       { presetSelect->setDisabled(dis); }
	if (resolutionSelect)   { resolutionSelect->setDisabled(dis); }
	if (waterSlider)        { waterSlider->setDisabled(dis); }
	if (platesSlider)       { platesSlider->setDisabled(dis); }
	if (radiusSlider)       { radiusSlider->setDisabled(dis); }
	if (rotationSlider)     { rotationSlider->setDisabled(dis); }
	if (ageSlider)          { ageSlider->setDisabled(dis); }
	if (atmosphereSlider)   { atmosphereSlider->setDisabled(dis); }
	if (starTempSlider)     { starTempSlider->setDisabled(dis); }
	if (semiMajorSlider)    { semiMajorSlider->setDisabled(dis); }
	if (eccentricitySlider) { eccentricitySlider->setDisabled(dis); }
	if (randomizeButton)    { randomizeButton->setDisabled(dis); }
	if (seedInput)          { seedInput->setEnabled(!dis); }
}

void ParameterPanel::syncValues(
	double   waterPercent,
	int      tectonicPlates,
	double   planetRadius,
	double   rotationRate,
	double   planetAge,
	double   atmosphere,
	double   starTemperature,
	double   semiMajorAxis,
	double   eccentricity,
	uint64_t seed)
{
	if (waterSlider)        { waterSlider->setValue(waterPercent); }
	if (platesSlider)       { platesSlider->setValue(static_cast<double>(tectonicPlates)); }
	if (radiusSlider)       { radiusSlider->setValue(planetRadius); }
	if (rotationSlider)     { rotationSlider->setValue(rotationRate); }
	if (ageSlider)          { ageSlider->setValue(planetAge); }
	if (atmosphereSlider)   { atmosphereSlider->setValue(atmosphere); }
	if (starTempSlider)     { starTempSlider->setValue(starTemperature); }
	if (semiMajorSlider)    { semiMajorSlider->setValue(semiMajorAxis); }
	if (eccentricitySlider) { eccentricitySlider->setValue(eccentricity); }
	if (seedInput)          { seedInput->setText(std::to_string(seed)); }
}

void ParameterPanel::setResolutionValue(const std::string& value) {
	if (resolutionSelect) {
		resolutionSelect->setValue(value);
	}
}

bool ParameterPanel::seedIsEmpty() const {
	return seedState == SeedState::Empty;
}

bool ParameterPanel::isSeedFocused() const {
	return seedInput && UI::FocusManager::Get().hasFocus(seedInput.get());
}

void ParameterPanel::onSeedTextChanged(const std::string& text) {
	if (text.empty()) {
		seedState = SeedState::Empty;
	} else {
		bool digitsOnly = std::all_of(text.begin(), text.end(),
			[](unsigned char c) { return std::isdigit(c); });
		uint64_t parsed = 0;
		bool parses = false;
		if (digitsOnly) {
			try {
				parsed = std::stoull(text);
				parses = true;
			} catch (...) {}
		}
		seedState = parses ? SeedState::Valid : SeedState::Invalid;
		if (parses && callbacks.onSeedChanged) {
			callbacks.onSeedChanged(std::to_string(parsed));
		}
	}

	// Red border while invalid
	if (seedInput) {
		seedInput->style.borderColor = (seedState == SeedState::Invalid)
			? Foundation::Color{0.85F, 0.3F, 0.3F, 1.0F}
			: UI::TextInputStyle{}.borderColor;
	}
	applyGenerateEnabled();
}

void ParameterPanel::applyGenerateEnabled() {
	if (generateButton) {
		generateButton->setDisabled(seedState == SeedState::Invalid);
	}
}

void ParameterPanel::update(float dt) {
	if (presetSelect)       { presetSelect->update(dt); }
	if (waterSlider)        { waterSlider->update(dt); }
	if (platesSlider)       { platesSlider->update(dt); }
	if (radiusSlider)       { radiusSlider->update(dt); }
	if (rotationSlider)     { rotationSlider->update(dt); }
	if (ageSlider)          { ageSlider->update(dt); }
	if (atmosphereSlider)   { atmosphereSlider->update(dt); }
	if (starTempSlider)     { starTempSlider->update(dt); }
	if (semiMajorSlider)    { semiMajorSlider->update(dt); }
	if (eccentricitySlider) { eccentricitySlider->update(dt); }
	if (resolutionSelect)   { resolutionSelect->update(dt); }
	if (seedInput)          { seedInput->update(dt); }
}

void ParameterPanel::render() {
	// Panel background
	Renderer::Primitives::drawRect({
		.bounds = {position.x, position.y, kPanelWidth, 700.0F},
		.style = {
			.fill = Foundation::Color{0.1F, 0.1F, 0.13F, 1.0F},
			.border = Foundation::BorderStyle{
				.color = Foundation::Color{0.25F, 0.25F, 0.3F, 1.0F},
				.width = 1.0F,
			},
		},
		.id = "param_panel_bg",
	});

	// Section labels
	for (auto& label : sectionLabels) {
		label.render();
	}

	if (waterSlider)        { waterSlider->render(); }
	if (platesSlider)       { platesSlider->render(); }
	if (radiusSlider)       { radiusSlider->render(); }
	if (rotationSlider)     { rotationSlider->render(); }
	if (ageSlider)          { ageSlider->render(); }
	if (atmosphereSlider)   { atmosphereSlider->render(); }
	if (starTempSlider)     { starTempSlider->render(); }
	if (semiMajorSlider)    { semiMajorSlider->render(); }
	if (eccentricitySlider) { eccentricitySlider->render(); }
	if (seedInput)          { seedInput->render(); }
	if (seedState == SeedState::Invalid) {
		UI::Text seedError(UI::Text::Args{
			.position = {position.x + kSliderX, seedErrorY},
			.text = "Seed must be a valid 64-bit number",
			.style = {
				.color = Foundation::Color{0.9F, 0.4F, 0.4F, 1.0F},
				.fontSize = 11.0F,
				.hAlign = Foundation::HorizontalAlign::Left,
				.vAlign = Foundation::VerticalAlign::Top,
			},
		});
		seedError.render();
	}
	if (randomizeButton)    { randomizeButton->render(); }
	if (generateButton && generateButton->visible) { generateButton->render(); }
	if (cancelButton && cancelButton->visible)     { cancelButton->render(); }

	// Dropdowns last: the batch renderer draws in submission order, so open
	// menus must be painted after the widgets they overlap (mirrors the
	// reverse hit-test order in handleEvent).
	if (presetSelect)       { presetSelect->render(); }
	if (resolutionSelect)   { resolutionSelect->render(); }
}

bool ParameterPanel::handleEvent(UI::InputEvent& event) {
	if (generating) {
		// Only cancel button active while generating
		if (cancelButton && cancelButton->visible) {
			if (cancelButton->handleEvent(event)) return true;
		}
		return false;
	}

	// Highest z-order first: dropdowns before sliders
	if (presetSelect && presetSelect->handleEvent(event)) return true;
	if (resolutionSelect && resolutionSelect->handleEvent(event)) return true;
	if (seedInput && seedInput->handleEvent(event)) return true;
	if (randomizeButton && randomizeButton->handleEvent(event)) return true;
	if (generateButton && generateButton->visible && generateButton->handleEvent(event)) return true;

	if (waterSlider && waterSlider->handleEvent(event)) return true;
	if (platesSlider && platesSlider->handleEvent(event)) return true;
	if (radiusSlider && radiusSlider->handleEvent(event)) return true;
	if (rotationSlider && rotationSlider->handleEvent(event)) return true;
	if (ageSlider && ageSlider->handleEvent(event)) return true;
	if (atmosphereSlider && atmosphereSlider->handleEvent(event)) return true;
	if (starTempSlider && starTempSlider->handleEvent(event)) return true;
	if (semiMajorSlider && semiMajorSlider->handleEvent(event)) return true;
	if (eccentricitySlider && eccentricitySlider->handleEvent(event)) return true;

	return false;
}

} // namespace world_sim
