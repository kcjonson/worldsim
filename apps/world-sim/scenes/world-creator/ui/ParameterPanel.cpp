#include "ParameterPanel.h"

#include "scenes/shared/SalalUi.h"

#include <ui/components/Button.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <utility>

namespace world_sim {

namespace {
	namespace family = ui::tokens::font::family;

	constexpr float kRowHeight = 28.0F;
	constexpr float kItemSpacing = 6.0F;
	constexpr float kSectionGap = 6.0F; // on top of kItemSpacing, before a section
}

void ParameterPanel::ScaledSlider::set(double value) const {
	if (ui::Slider* slider = ref.get()) {
		slider->setValue(static_cast<float>(log ? std::log10(value) : value));
	}
}

ParameterPanel::ParameterPanel(ParameterPanelCallbacks cbs)
	: callbacks(std::move(cbs)) {}

void ParameterPanel::addLabel(ui::Ref<ui::Component> parent, const std::string& text) const {
	parent.addChild<ui::Text>({
		.margin = {kSectionGap, 0.0F, 0.0F, 0.0F},
		.style = world_sim::text(
			*theme,
			{.family = family::mono,
			 .size = tokens::fontSize::fs2xs,
			 .color = tokens::color::textDim,
			 .tracking = tokens::letterSpacing::lsWider,
			 .transform = Foundation::TextTransform::Uppercase}
		),
		.text = text,
	});
}

ParameterPanel::ScaledSlider ParameterPanel::addSlider(
	ui::Ref<ui::Component>			   parent,
	double							   min,
	double							   max,
	double							   step,
	double							   value,
	bool							   logScale,
	const std::string&				   label,
	std::function<std::string(double)> formatter,
	std::function<void(double)>		   onChange
) const {
	const auto toTrack = [logScale](double v) {
		return static_cast<float>(logScale ? std::log10(v) : v);
	};
	const auto fromTrack = [logScale](float t) {
		return logScale ? std::pow(10.0, static_cast<double>(t)) : static_cast<double>(t);
	};
	ScaledSlider out{.log = logScale};
	out.ref = parent.addChild<ui::Slider>({
		.size = {ui::percent(100.0F), ui::kAuto},
		.min = toTrack(min),
		.max = toTrack(max),
		.step = static_cast<float>(step),
		.value = toTrack(value),
		.label = label,
		.showValue = true,
		.format = [formatter = std::move(formatter), fromTrack](float t) { return formatter(fromTrack(t)); },
		.onChange =
			[onChange = std::move(onChange), fromTrack](float t) {
				if (onChange) {
					onChange(fromTrack(t));
				}
			},
	});
	return out;
}

void ParameterPanel::build(ui::Ref<ui::Component> body, const ui::Theme& themeRef) {
	theme = &themeRef;
	column = body.addChild<ui::Component>({
		.id = "parameters",
		.size = {ui::percent(100.0F), 0.0F},
		.layout = {.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .gap = kItemSpacing, .flexGrow = 1.0F},
		.overflow = ui::Overflow::Scroll,
	});

	// Preset grid: 2 columns, the active preset selected.
	addLabel(column, "Preset");
	ui::Ref<ui::Component> row;
	for (size_t i = 0; i < kPresets.size(); ++i) {
		if (i % 2 == 0) {
			row = column.addChild<ui::Component>({
				.size = {ui::percent(100.0F), kRowHeight},
				.layout = {.display = ui::Display::Flex, .gap = 8.0F},
			});
		}
		const std::string value = kPresets[i].value;
		presetRows[i] = row.addChild<ui::ListRow>({
			.id = value,
			.size = {0.0F, ui::percent(100.0F)},
			.layout = {.flexGrow = 1.0F},
			.label = kPresets[i].label,
			.selected = value == activePreset,
			.onPress = [this, value] { applyPreset(value); },
		});
	}

	addLabel(column, "Planet Properties");
	waterSlider = addSlider(
		column, 0.0, 100.0, 1.0, 70.0, false, "Water %", [](double v) { return std::format("{:.0f}%", v); }, callbacks.onWaterAmount
	);
	platesSlider = addSlider(
		column,
		2.0,
		30.0,
		1.0,
		12.0,
		false,
		"Tectonic Plates",
		[](double v) { return std::format("{:.0f}", v); },
		callbacks.onTectonicPlates
	);
	radiusSlider = addSlider(
		column, 0.1, 10.0, 0.0, 1.0, true, "Radius (Re)", [](double v) { return std::format("{:.2f}", v); }, callbacks.onPlanetRadius
	);
	rotationSlider = addSlider(
		column, 0.1, 100.0, 0.0, 1.0, true, "Rotation (d)", [](double v) { return std::format("{:.2f}", v); }, callbacks.onRotationRate
	);
	ageSlider = addSlider(
		column,
		1.0e7,
		1.0e10,
		0.0,
		4.5e9,
		true,
		"Planet Age (yr)",
		[](double v) {
			if (v >= 1.0e9)
				return std::format("{:.1f}B yr", v / 1.0e9);
			return std::format("{:.0f}M yr", v / 1.0e6);
		},
		callbacks.onPlanetAge
	);
	atmosphereSlider = addSlider(
		column, 0.1, 10.0, 0.0, 1.0, true, "Atmosphere", [](double v) { return std::format("{:.2f} atm", v); }, callbacks.onAtmosphere
	);
	// Prototype-only control: the generator has no mean-temp input yet.
	const ScaledSlider meanTemp =
		addSlider(column, -20.0, 40.0, 1.0, 14.0, false, "Mean Temp", [](double v) { return std::format("{:.0f} C", v); }, nullptr);
	if (ui::Slider* slider = meanTemp.ref.get()) {
		slider->setEnabled(false);
	}

	// Seed row
	ui::Ref<ui::Component> seedRow = column.addChild<ui::Component>({
		.size = {ui::percent(100.0F), kRowHeight},
		.margin = {kSectionGap, 0.0F, 0.0F, 0.0F},
		.layout = {.display = ui::Display::Flex, .gap = 8.0F},
	});
	seedInput = seedRow.addChild<ui::TextInput>({
		.id = "seed_input",
		.size = {0.0F, ui::percent(100.0F)},
		.layout = {.flexGrow = 1.0F},
		.placeholder = "Random seed...",
		.onChange = [this](const std::string& text) { onSeedTextChanged(text); },
	});
	seedRow.addChild<ui::Button>({
		.id = "btn_randomize",
		.size = {90.0F, ui::percent(100.0F)},
		.label = "Random",
		.controlSize = ui::ControlSize::Sm,
		.onPress = [this] {
			if (callbacks.onRandomize) {
				callbacks.onRandomize();
			}
		},
	});
	seedError = column.addChild<ui::Text>({
		.id = "seed_error",
		.visible = false,
		.style = world_sim::text(*theme, {.size = tokens::fontSize::fsXs, .color = tokens::color::statusCrit}),
		.text = "Seed must be a valid 64-bit number",
	});

	// Advanced (collapsed): resolution + star/orbit sliders.
	advancedToggle = column.addChild<ui::ListRow>({
		.id = "advanced_toggle",
		.size = {ui::percent(100.0F), kRowHeight},
		.margin = {kSectionGap, 0.0F, 0.0F, 0.0F},
		.label = "Advanced",
		.trailing = "Show",
		.dim = true,
		.onPress = [this] { toggleAdvanced(); },
	});
	advanced = column.addChild<ui::Component>({
		.id = "advanced",
		.visible = false,
		.layout = {.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .gap = kItemSpacing},
	});
	resolutionSelect = advanced.addChild<ui::Select>({
		.id = "resolution_select",
		.size = {ui::percent(100.0F), ui::kAuto},
		.options =
			{
				{"Preview 256", "256"},
				{"Low 512", "512"},
				{"Default 1024", "1024"},
				{"High 1449", "1449"},
				{"Ultra 2048", "2048"},
			},
		.value = "256",
		.onChange = [this](const std::string& value) {
			if (callbacks.onResolutionChanged) {
				callbacks.onResolutionChanged(value);
			}
		},
	});
	starTempSlider = addSlider(
		advanced,
		2000.0,
		50000.0,
		0.0,
		5778.0,
		true,
		"Star Temp (K)",
		[](double v) { return std::format("{:.0f}K", v); },
		callbacks.onStarTemperature
	);
	semiMajorSlider = addSlider(
		advanced,
		0.1,
		100.0,
		0.0,
		1.0,
		true,
		"Semi-Major (AU)",
		[](double v) { return std::format("{:.2f} AU", v); },
		callbacks.onSemiMajorAxis
	);
	eccentricitySlider = addSlider(
		advanced, 0.0, 0.95, 0.0, 0.017, false, "Eccentricity", [](double v) { return std::format("{:.3f}", v); }, callbacks.onEccentricity
	);
}

void ParameterPanel::applyPreset(const std::string& value) {
	activePreset = value;
	for (size_t i = 0; i < kPresets.size(); ++i) {
		if (ui::ListRow* shown = presetRows[i].get()) {
			shown->setSelected(kPresets[i].value == activePreset);
		}
	}
	if (callbacks.onPresetChanged) callbacks.onPresetChanged(value);
}

void ParameterPanel::toggleAdvanced() {
	ui::Component* section = advanced.get();
	if (section == nullptr) {
		return;
	}
	const bool open = !section->isVisible();
	section->setVisible(open);
	if (ui::ListRow* toggle = advancedToggle.get()) {
		toggle->setTrailing(open ? "Hide" : "Show");
	}
}

void ParameterPanel::setGenerating(bool gen) {
	if (ui::Component* shown = column.get()) {
		shown->setEnabled(!gen);
	}
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
	waterSlider.set(waterPercent);
	platesSlider.set(static_cast<double>(tectonicPlates));
	radiusSlider.set(planetRadius);
	rotationSlider.set(rotationRate);
	ageSlider.set(planetAge);
	atmosphereSlider.set(atmosphere);
	starTempSlider.set(starTemperature);
	semiMajorSlider.set(semiMajorAxis);
	eccentricitySlider.set(eccentricity);
	// setText doesn't call onChange, so the seed's state follows by hand.
	const std::string text = std::to_string(seed);
	if (ui::TextInput* input = seedInput.get()) {
		input->setText(text);
	}
	onSeedTextChanged(text);
}

void ParameterPanel::setResolutionValue(const std::string& value) {
	if (ui::Select* select = resolutionSelect.get()) {
		select->setValue(value);
	}
}

bool ParameterPanel::seedIsEmpty() const {
	return seedState == SeedState::Empty;
}

bool ParameterPanel::isSeedFocused() const {
	const ui::TextInput* input = seedInput.get();
	return input != nullptr && input->isFocused();
}

void ParameterPanel::onSeedTextChanged(const std::string& text) {
	const bool wasInvalid = seedState == SeedState::Invalid;
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
	const bool invalid = seedState == SeedState::Invalid;
	if (invalid == wasInvalid) {
		return;
	}

	// Red border and the error line while invalid
	if (ui::TextInput* input = seedInput.get()) {
		ui::Style style = input->getDeclaredStyle();
		style.borderColor = invalid ? std::optional(theme->get(tokens::color::statusCrit)) : std::nullopt;
		input->setStyle(style);
	}
	if (ui::Text* error = seedError.get()) {
		error->setVisible(invalid);
	}
}

} // namespace world_sim
