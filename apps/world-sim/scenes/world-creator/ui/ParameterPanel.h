#pragma once

#include "component/Container.h"
#include "components/TextInput/TextInput.h"
#include "components/button/Button.h"
#include "components/select/Select.h"
#include "components/slider/Slider.h"
#include "input/InputEvent.h"
#include "math/Types.h"
#include "primitives/Primitives.h"
#include "shapes/Shapes.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

// ParameterPanel - left-side control panel for WorldCreatorScene.
//
// Fixed 320px wide. Shows preset selector, all planet sliders, seed input,
// Generate and Cancel buttons. Disabled (inputs grayed) while Generating.

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
	std::function<void()> onGenerate;
	std::function<void()> onCancel;
};

class ParameterPanel {
  public:
	explicit ParameterPanel(Foundation::Vec2 position, ParameterPanelCallbacks callbacks);

	void update(float deltaTime);
	void render();
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

  private:
	enum class SeedState { Empty, Valid, Invalid };

	Foundation::Vec2 position;
	ParameterPanelCallbacks callbacks;
	bool generating{false};
	SeedState seedState{SeedState::Empty};
	float seedErrorY{0.0F};

	static constexpr float kPanelWidth = 320.0F;
	static constexpr float kLabelHeight = 16.0F;
	static constexpr float kSliderHeight = 32.0F;
	static constexpr float kItemSpacing = 6.0F;
	static constexpr float kSectionSpacing = 12.0F;

	// UI elements (ordered for layout / event dispatch)
	std::unique_ptr<UI::Select>	   presetSelect;
	std::unique_ptr<UI::Slider>	   waterSlider;
	std::unique_ptr<UI::Slider>	   platesSlider;
	std::unique_ptr<UI::Slider>	   radiusSlider;
	std::unique_ptr<UI::Slider>	   rotationSlider;
	std::unique_ptr<UI::Slider>	   ageSlider;
	std::unique_ptr<UI::Slider>	   atmosphereSlider;
	std::unique_ptr<UI::Slider>	   starTempSlider;
	std::unique_ptr<UI::Slider>	   semiMajorSlider;
	std::unique_ptr<UI::Slider>	   eccentricitySlider;
	std::unique_ptr<UI::Select>	   resolutionSelect;
	std::unique_ptr<UI::TextInput> seedInput;
	std::unique_ptr<UI::Button>	   randomizeButton;
	std::unique_ptr<UI::Button>	   generateButton;
	std::unique_ptr<UI::Button>	   cancelButton;

	// Section labels
	std::vector<Renderer::Primitives::TextArgs> sectionLabels;

	void buildWidgets();
	void onSeedTextChanged(const std::string& text);
	void applyGenerateEnabled();
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
