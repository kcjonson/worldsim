#pragma once

#include "components/TextInput/TextInput.h"
#include "components/button/Button.h"
#include "components/select/Select.h"
#include "components/slider/Slider.h"
#include "input/InputEvent.h"
#include "math/Types.h"
#include "primitives/Primitives.h"
#include "shapes/Shapes.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// ParameterPanel - left-side control panel for WorldCreatorScene.
//
// Fixed 320px wide, framed by a data-accent Panel ("Parameters" / "Survey
// Config"). A 2-column preset button grid (active preset data-tinted), the
// planet sliders (plus a disabled prototype-only Mean Temp), the seed row,
// and a collapsible Advanced section at the bottom holding the resolution
// select and the star/orbit sliders. Inputs gray out while Generating.
// Generate/Cancel live in the scene footer, not here.

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
	explicit ParameterPanel(Foundation::Vec2 position, ParameterPanelCallbacks callbacks);

	void update(float deltaTime);
	void render(float height);
	bool handleEvent(UI::InputEvent& event);

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

	Foundation::Vec2 position;
	ParameterPanelCallbacks callbacks;
	bool generating{false};
	bool advancedOpen{false};
	SeedState seedState{SeedState::Empty};
	float seedErrorY{0.0F};

	static constexpr float kLabelHeight = 16.0F;
	static constexpr float kSliderHeight = 32.0F;
	static constexpr float kItemSpacing = 6.0F;
	static constexpr float kSectionSpacing = 12.0F;

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

	// UI elements (ordered for layout / event dispatch)
	std::array<std::unique_ptr<UI::Button>, 6> presetButtons;
	std::string								   activePreset{"earth_like"};

	std::unique_ptr<UI::Slider>	   waterSlider;
	std::unique_ptr<UI::Slider>	   platesSlider;
	std::unique_ptr<UI::Slider>	   radiusSlider;
	std::unique_ptr<UI::Slider>	   rotationSlider;
	std::unique_ptr<UI::Slider>	   ageSlider;
	std::unique_ptr<UI::Slider>	   atmosphereSlider;
	std::unique_ptr<UI::Slider>	   meanTempSlider; // prototype-only, always disabled
	std::unique_ptr<UI::TextInput> seedInput;
	std::unique_ptr<UI::Button>	   randomizeButton;

	// Advanced section (collapsed by default; fully functional when open)
	std::unique_ptr<UI::Select> resolutionSelect;
	std::unique_ptr<UI::Slider> starTempSlider;
	std::unique_ptr<UI::Slider> semiMajorSlider;
	std::unique_ptr<UI::Slider> eccentricitySlider;
	Foundation::Rect			advancedToggleBounds{};

	// Section labels
	std::vector<Renderer::Primitives::TextArgs> sectionLabels;

	void buildWidgets();
	void applyPreset(const std::string& value);
	void setAdvancedOpen(bool open);
	void onSeedTextChanged(const std::string& text);
	float nextY{0.0F}; // layout cursor

	Renderer::Primitives::TextArgs makeLabel(const std::string& text, float y);
	float addLabel(const std::string& text);
	float addSlider(std::unique_ptr<UI::Slider>& out,
	                double min, double max, double step, double value,
	                bool logScale, const std::string& label,
	                std::function<std::string(double)> formatter,
	                std::function<void(double)> onChange);
};

} // namespace world_sim
