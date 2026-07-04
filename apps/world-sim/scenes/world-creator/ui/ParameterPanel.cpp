#include "ParameterPanel.h"

#include "primitives/Primitives.h"
#include <components/icon/Icon.h>
#include <components/panel/Panel.h>
#include <theme/Tokens.h>
#include <theme/Variants.h>

#include <focus/FocusManager.h>

#include <algorithm>
#include <cctype>
#include <format>
#include <string>
#include <utility>
#include <vector>

namespace world_sim {

namespace {
	// Common slider width inside the panel
	constexpr float kSliderWidth = 296.0F;
	constexpr float kSliderX = 12.0F; // offset from panel left
}

ParameterPanel::ParameterPanel(Foundation::Vec2 pos, ParameterPanelCallbacks cbs)
	: position(pos), callbacks(std::move(cbs)) {
	buildWidgets();
}

Renderer::Primitives::TextArgs ParameterPanel::makeLabel(const std::string& text, float y) {
	return Renderer::Primitives::TextArgs{
		.text = text,
		.position = {position.x + kSliderX, y},
		.scale = UI::fs_2xs / 16.0F,
		.color = UI::text_dim,
		.font = UI::fontMono,
		.vAlign = Foundation::VerticalAlign::Top,
		.letterSpacing = UI::fs_2xs * UI::ls_wider,
		.transform = Foundation::TextTransform::Uppercase,
	};
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
	// Content starts below the Panel header band.
	nextY = 58.0F;

	// Preset grid: 2 columns, active preset carries the data tint.
	addLabel("Preset");
	{
		const float gridY = position.y + nextY;
		const float btnW = (kSliderWidth - 8.0F) * 0.5F;
		constexpr float kBtnH = 28.0F;
		for (size_t i = 0; i < kPresets.size(); ++i) {
			const float bx = position.x + kSliderX + (i % 2 == 0 ? 0.0F : btnW + 8.0F);
			const float by = gridY + static_cast<float>(i / 2) * (kBtnH + 6.0F);
			const std::string value = kPresets[i].value;
			presetButtons[i] = std::make_unique<UI::Button>(UI::Button::Args{
				.label = kPresets[i].label,
				.position = {bx, by},
				.size = {btnW, kBtnH},
				.type = value == activePreset ? UI::Button::Type::Data : UI::Button::Type::Secondary,
				.onClick = [this, value]() { applyPreset(value); },
				.id = kPresets[i].value,
			});
		}
		nextY += 3.0F * kBtnH + 2.0F * 6.0F + kSectionSpacing;
	}

	// Planet Properties
	addLabel("Planet Properties");
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

	// Prototype-only control: the generator has no mean-temp input yet.
	addSlider(meanTempSlider, -20.0, 40.0, 1.0, 14.0, false,
		"Mean Temp",
		[](double v) { return std::format("{:.0f} C", v); },
		nullptr);
	meanTempSlider->setDisabled(true);

	nextY += kSectionSpacing;

	// Seed row
	float seedY = position.y + nextY;
	seedInput = std::make_unique<UI::TextInput>(UI::TextInput::Args{
		.position = {position.x + kSliderX, seedY},
		.size = {kSliderWidth - 98.0F, 28.0F},
		.placeholder = "Random seed...",
		.id = "seed_input",
		.onChange = [this](const std::string& text) { onSeedTextChanged(text); },
	});

	randomizeButton = std::make_unique<UI::Button>(UI::Button::Args{
		.label = "Random",
		.position = {position.x + kSliderX + kSliderWidth - 90.0F, seedY},
		.size = {90.0F, 28.0F},
		.type = UI::Button::Type::Secondary,
		.onClick = callbacks.onRandomize,
		.id = "btn_randomize",
	});
	nextY += 28.0F + 2.0F;
	seedErrorY = position.y + nextY;
	nextY += kLabelHeight + kSectionSpacing - 2.0F;

	// Advanced (collapsed): resolution + star/orbit sliders at the panel bottom.
	advancedToggleBounds = {position.x + kSliderX, position.y + nextY, kSliderWidth, 20.0F};
	nextY += 20.0F + kItemSpacing;

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

	setAdvancedOpen(false);
}

void ParameterPanel::applyPreset(const std::string& value) {
	activePreset = value;
	for (size_t i = 0; i < kPresets.size(); ++i) {
		if (presetButtons[i]) {
			presetButtons[i]->type =
				kPresets[i].value == activePreset ? UI::Button::Type::Data : UI::Button::Type::Secondary;
		}
	}
	if (callbacks.onPresetChanged) callbacks.onPresetChanged(value);
}

void ParameterPanel::setAdvancedOpen(bool open) {
	advancedOpen = open;
	if (resolutionSelect)   { resolutionSelect->visible = open; }
	if (starTempSlider)     { starTempSlider->visible = open; }
	if (semiMajorSlider)    { semiMajorSlider->visible = open; }
	if (eccentricitySlider) { eccentricitySlider->visible = open; }
}

void ParameterPanel::setGenerating(bool gen) {
	generating = gen;

	// Disable all parameter controls during generation
	bool dis = gen;
	for (auto& preset : presetButtons) {
		if (preset) { preset->setDisabled(dis); }
	}
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
			? UI::status_crit
			: UI::TextInputStyle{}.borderColor;
	}
}

void ParameterPanel::update(float dt) {
	for (auto& preset : presetButtons) {
		if (preset) { preset->update(dt); }
	}
	if (waterSlider)        { waterSlider->update(dt); }
	if (platesSlider)       { platesSlider->update(dt); }
	if (radiusSlider)       { radiusSlider->update(dt); }
	if (rotationSlider)     { rotationSlider->update(dt); }
	if (ageSlider)          { ageSlider->update(dt); }
	if (atmosphereSlider)   { atmosphereSlider->update(dt); }
	if (advancedOpen) {
		if (starTempSlider)     { starTempSlider->update(dt); }
		if (semiMajorSlider)    { semiMajorSlider->update(dt); }
		if (eccentricitySlider) { eccentricitySlider->update(dt); }
		if (resolutionSelect)   { resolutionSelect->update(dt); }
	}
	if (seedInput) { seedInput->update(dt); }
}

void ParameterPanel::render(float height) {
	UI::Panel frame({.position = position,
					 .size = {kPanelWidth, height},
					 .title = "Parameters",
					 .kicker = "Survey Config",
					 .accent = UI::PanelAccent::Data});
	frame.render();

	for (const auto& label : sectionLabels) {
		Renderer::Primitives::drawText(label);
	}

	for (auto& preset : presetButtons) {
		if (preset) { preset->render(); }
	}
	if (waterSlider)        { waterSlider->render(); }
	if (platesSlider)       { platesSlider->render(); }
	if (radiusSlider)       { radiusSlider->render(); }
	if (rotationSlider)     { rotationSlider->render(); }
	if (ageSlider)          { ageSlider->render(); }
	if (atmosphereSlider)   { atmosphereSlider->render(); }
	if (meanTempSlider)     { meanTempSlider->render(); }
	if (seedInput)          { seedInput->render(); }
	if (seedState == SeedState::Invalid) {
		UI::Text seedError(UI::Text::Args{
			.position = {position.x + kSliderX, seedErrorY},
			.text = "Seed must be a valid 64-bit number",
			.style = {
				.color = UI::status_crit,
				.fontSize = UI::fs_xs,
				.hAlign = Foundation::HorizontalAlign::Left,
				.vAlign = Foundation::VerticalAlign::Top,
			},
		});
		seedError.render();
	}
	if (randomizeButton)    { randomizeButton->render(); }

	// Advanced toggle row: stencil label + open/close chevron.
	Renderer::Primitives::drawText({
		.text = "Advanced",
		.position = {advancedToggleBounds.x, advancedToggleBounds.y + 3.0F},
		.scale = UI::fs_2xs / 16.0F,
		.color = advancedOpen ? UI::text : UI::text_dim,
		.font = UI::fontMono,
		.vAlign = Foundation::VerticalAlign::Top,
		.letterSpacing = UI::fs_2xs * UI::ls_wider,
		.transform = Foundation::TextTransform::Uppercase,
	});
	UI::Icon chevron({.position = {advancedToggleBounds.right() - 16.0F, advancedToggleBounds.y + 2.0F},
					  .size = 14.0F,
					  .glyph = advancedOpen ? "chevronUp" : "chevronDown",
					  .tint = UI::text_dim});
	chevron.render();

	if (advancedOpen) {
		if (starTempSlider)     { starTempSlider->render(); }
		if (semiMajorSlider)    { semiMajorSlider->render(); }
		if (eccentricitySlider) { eccentricitySlider->render(); }
	}

	// Dropdowns last: the batch renderer draws in submission order, so open
	// menus must be painted after the widgets they overlap (mirrors the
	// reverse hit-test order in handleEvent).
	if (advancedOpen && resolutionSelect) { resolutionSelect->render(); }
}

bool ParameterPanel::handleEvent(UI::InputEvent& event) {
	if (generating) {
		return false;
	}

	// Highest z-order first: dropdowns before sliders
	if (advancedOpen && resolutionSelect && resolutionSelect->handleEvent(event)) return true;
	if (seedInput && seedInput->handleEvent(event)) return true;
	if (randomizeButton && randomizeButton->handleEvent(event)) return true;
	for (auto& preset : presetButtons) {
		if (preset && preset->handleEvent(event)) return true;
	}

	// Advanced toggle.
	if (event.type == UI::InputEvent::Type::MouseUp && event.button == engine::MouseButton::Left &&
	    advancedToggleBounds.contains(event.position)) {
		setAdvancedOpen(!advancedOpen);
		event.consume();
		return true;
	}

	if (waterSlider && waterSlider->handleEvent(event)) return true;
	if (platesSlider && platesSlider->handleEvent(event)) return true;
	if (radiusSlider && radiusSlider->handleEvent(event)) return true;
	if (rotationSlider && rotationSlider->handleEvent(event)) return true;
	if (ageSlider && ageSlider->handleEvent(event)) return true;
	if (atmosphereSlider && atmosphereSlider->handleEvent(event)) return true;
	if (advancedOpen) {
		if (starTempSlider && starTempSlider->handleEvent(event)) return true;
		if (semiMajorSlider && semiMajorSlider->handleEvent(event)) return true;
		if (eccentricitySlider && eccentricitySlider->handleEvent(event)) return true;
	}

	return false;
}

} // namespace world_sim
