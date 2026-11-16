#pragma once

#include <ui/Component.h>
#include <ui/components/ListRow.h>
#include <ui/components/Select.h>
#include <ui/components/Slider.h>
#include <ui/components/TextInput.h>
#include <ui/shapes/Text.h>
#include <ui/theme/Theme.h>

#include <array>
#include <cstdint>
#include <functional>
#include <string>

// ParameterPanel - the world creator's parameter sidebar, built into a panel's body.
//
// A 2-column preset grid (the active preset selected), the planet sliders (plus a disabled
// prototype-only Mean Temp), the seed row, and a collapsible Advanced section holding the resolution
// select and the star/orbit sliders, all in one scrolling column. The scene adds Generate and Cancel
// under it. Inputs disable while Generating.

namespace world_sim {

struct ParameterPanelCallbacks {
	std::function<void(const std::string&)> onPresetChanged;
	std::function<void(double)> onWaterAmount;
	std::function<void(double)> onTectonicPlates;
	std::function<void(double)> onPlanetRadius;
	std::function<void(double)> onRotationRate;
	std::function<void(double)> onPlanetAge;
	std::function<void(double)> onAtmosphere;
	std::function<void(double)> onStarTemperature;
	std::function<void(double)> onSemiMajorAxis;
	std::function<void(double)> onEccentricity;
	std::function<void(const std::string&)> onResolutionChanged;
	std::function<void(const std::string&)> onSeedChanged;
	std::function<void()> onRandomize;
};

class ParameterPanel {
  public:
	explicit ParameterPanel(ParameterPanelCallbacks callbacks);

	// Fills `body`, a flex column, with the scrolling parameter column.
	void build(ui::Ref<ui::Component> body, const ui::Theme& theme);

	void setGenerating(bool generating);

	// Sync displayed values from model (called after preset changes).
	// waterPercent is in [0,100]; seed is displayed as decimal string.
	void syncValues(
		double   waterPercent,
		int      tectonicPlates,
		double   planetRadius,
		double   rotationRate,
		double   planetAge,
		double   atmosphere,
		double   starTemperature,
		double   semiMajorAxis,
		double   eccentricity,
		uint64_t seed
	);

	// Sync the resolution select (value is the subdivision as a string,
	// e.g. "256"); syncValues does not cover it
	void setResolutionValue(const std::string& value);

	// True when the seed field is blank (caller should pick a random seed)
	bool seedIsEmpty() const;

	// False while the seed text is non-empty and not a valid uint64
	bool seedIsValid() const { return seedState != SeedState::Invalid; }

	// True while the seed field holds keyboard focus, so the scene can suppress
	// WASD/arrow camera panning while the user is typing a seed.
	bool isSeedFocused() const;

	static constexpr float kPanelWidth = 360.0F;

  private:
	enum class SeedState { Empty, Valid, Invalid };

	// A slider over log10 of its range when `log`, so the track reads as the old log-scale sliders did.
	struct ScaledSlider {
		ui::Ref<ui::Slider> ref;
		bool				log = false;
		void				set(double value) const;
	};

	struct PresetDef {
		const char* label;
		const char* value;
	};
	static constexpr std::array<PresetDef, 6> kPresets{{
		{"Earth-Like", "earth_like"},
		{"Desert World", "desert_world"},
		{"Ocean World", "ocean_world"},
		{"Frozen World", "frozen_world"},
		{"Volcanic World", "volcanic_world"},
		{"Ancient Garden", "ancient_garden"},
	}};

	ParameterPanelCallbacks callbacks;
	const ui::Theme*		theme = nullptr;
	SeedState				seedState{SeedState::Empty};
	std::string				activePreset{"earth_like"};

	ui::Ref<ui::Component>				column;
	std::array<ui::Ref<ui::ListRow>, 6> presetRows;
	ScaledSlider						waterSlider;
	ScaledSlider						platesSlider;
	ScaledSlider						radiusSlider;
	ScaledSlider						rotationSlider;
	ScaledSlider						ageSlider;
	ScaledSlider						atmosphereSlider;
	ui::Ref<ui::TextInput>				seedInput;
	ui::Ref<ui::Text>					seedError;
	ui::Ref<ui::ListRow>				advancedToggle;
	ui::Ref<ui::Component>				advanced;
	ui::Ref<ui::Select>					resolutionSelect;
	ScaledSlider						starTempSlider;
	ScaledSlider						semiMajorSlider;
	ScaledSlider						eccentricitySlider;

	void		 applyPreset(const std::string& value);
	void		 toggleAdvanced();
	void		 onSeedTextChanged(const std::string& text);
	void		 addLabel(ui::Ref<ui::Component> parent, const std::string& text) const;
	ScaledSlider addSlider(
		ui::Ref<ui::Component>			   parent,
		double							   min,
		double							   max,
		double							   step,
		double							   value,
		bool							   logScale,
		const std::string&				   label,
		std::function<std::string(double)> formatter,
		std::function<void(double)>		   onChange
	) const;
};

} // namespace world_sim
