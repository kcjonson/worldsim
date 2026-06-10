#include "Slider.h"

#include "focus/FocusManager.h"
#include <cmath>
#include <gtest/gtest.h>

namespace UI {

class SliderTest : public ::testing::Test {
  protected:
	FocusManager focusManager;

	void SetUp() override {
		FocusManager::setInstance(&focusManager);
	}

	void TearDown() override {
		FocusManager::setInstance(nullptr);
	}
};

// === Construction Tests ===

TEST_F(SliderTest, ConstructsWithDefaults) {
	Slider slider(Slider::Args{
		.min = 0.0,
		.max = 1.0,
		.value = 0.5,
	});

	EXPECT_DOUBLE_EQ(slider.getValue(), 0.5);
	EXPECT_FALSE(slider.isDisabled());
}

TEST_F(SliderTest, ClampInitialValueToRange) {
	Slider low(Slider::Args{.min = 0.0, .max = 1.0, .value = -5.0});
	EXPECT_DOUBLE_EQ(low.getValue(), 0.0);

	Slider high(Slider::Args{.min = 0.0, .max = 1.0, .value = 99.0});
	EXPECT_DOUBLE_EQ(high.getValue(), 1.0);
}

// === Linear value <-> position math ===

TEST_F(SliderTest, LinearValueToPosition) {
	Slider slider(Slider::Args{.min = 0.0, .max = 100.0, .value = 50.0});

	EXPECT_DOUBLE_EQ(slider.valueToPosition(0.0), 0.0);
	EXPECT_DOUBLE_EQ(slider.valueToPosition(100.0), 1.0);
	EXPECT_DOUBLE_EQ(slider.valueToPosition(50.0), 0.5);
	EXPECT_DOUBLE_EQ(slider.valueToPosition(25.0), 0.25);
}

TEST_F(SliderTest, LinearPositionToValue) {
	Slider slider(Slider::Args{.min = 0.0, .max = 100.0, .value = 0.0});

	EXPECT_DOUBLE_EQ(slider.positionToValue(0.0), 0.0);
	EXPECT_DOUBLE_EQ(slider.positionToValue(1.0), 100.0);
	EXPECT_DOUBLE_EQ(slider.positionToValue(0.5), 50.0);
	EXPECT_DOUBLE_EQ(slider.positionToValue(0.25), 25.0);
}

TEST_F(SliderTest, LinearRoundTrip) {
	Slider slider(Slider::Args{.min = 10.0, .max = 50.0, .value = 30.0});

	double t = slider.valueToPosition(30.0);
	double roundTrip = slider.positionToValue(t);
	EXPECT_NEAR(roundTrip, 30.0, 1e-9);
}

// === Log scale value <-> position math ===

TEST_F(SliderTest, LogScaleValueToPositionAtExtremes) {
	Slider slider(Slider::Args{
		.min = 0.1, .max = 50.0, .value = 0.1, .logScale = true,
	});

	EXPECT_NEAR(slider.valueToPosition(0.1), 0.0, 1e-9);
	EXPECT_NEAR(slider.valueToPosition(50.0), 1.0, 1e-9);
}

TEST_F(SliderTest, LogScalePositionToValueAtExtremes) {
	Slider slider(Slider::Args{
		.min = 0.1, .max = 50.0, .value = 1.0, .logScale = true,
	});

	EXPECT_NEAR(slider.positionToValue(0.0), 0.1, 1e-9);
	EXPECT_NEAR(slider.positionToValue(1.0), 50.0, 1e-9);
}

TEST_F(SliderTest, LogScaleRoundTrip) {
	Slider slider(Slider::Args{
		.min = 0.1, .max = 50.0, .value = 5.0, .logScale = true,
	});

	double t = slider.valueToPosition(5.0);
	double roundTrip = slider.positionToValue(t);
	EXPECT_NEAR(roundTrip, 5.0, 1e-6);
}

TEST_F(SliderTest, LogScaleMidpointIsGeometricMean) {
	// At t=0.5, value should be geometric mean of min and max
	Slider slider(Slider::Args{
		.min = 1.0, .max = 100.0, .value = 10.0, .logScale = true,
	});

	double mid = slider.positionToValue(0.5);
	double geometricMean = std::sqrt(1.0 * 100.0);
	EXPECT_NEAR(mid, geometricMean, 1e-6);
}

// === Step snapping ===

TEST_F(SliderTest, StepSnappingRoundsToNearestStep) {
	Slider slider(Slider::Args{.min = 0.0, .max = 10.0, .step = 1.0, .value = 0.0});

	EXPECT_DOUBLE_EQ(slider.snapToStep(3.4), 3.0);
	EXPECT_DOUBLE_EQ(slider.snapToStep(3.6), 4.0);
	EXPECT_DOUBLE_EQ(slider.snapToStep(3.5), 4.0); // round half-up
}

TEST_F(SliderTest, StepSnappingClampsToRange) {
	Slider slider(Slider::Args{.min = 0.0, .max = 10.0, .step = 1.0, .value = 0.0});

	EXPECT_DOUBLE_EQ(slider.snapToStep(-1.0), 0.0);
	EXPECT_DOUBLE_EQ(slider.snapToStep(11.0), 10.0);
}

TEST_F(SliderTest, SetValueWithStepSnaps) {
	bool called = false;
	double lastVal = -1.0;
	Slider slider(Slider::Args{
		.min = 0.0, .max = 30.0, .step = 1.0, .value = 0.0,
		.onChanged = [&](double v) { called = true; lastVal = v; },
	});

	slider.setValue(7.4);
	EXPECT_DOUBLE_EQ(slider.getValue(), 7.0);
	EXPECT_TRUE(called);
	EXPECT_DOUBLE_EQ(lastVal, 7.0);
}

// === Clamping ===

TEST_F(SliderTest, SetValueClampsToRange) {
	Slider slider(Slider::Args{.min = 5.0, .max = 20.0, .value = 10.0});

	slider.setValue(100.0);
	EXPECT_DOUBLE_EQ(slider.getValue(), 20.0);

	slider.setValue(-10.0);
	EXPECT_DOUBLE_EQ(slider.getValue(), 5.0);
}

// === Callback firing ===

TEST_F(SliderTest, CallbackFiresOnValueChange) {
	int callCount = 0;
	double lastVal = -1.0;

	Slider slider(Slider::Args{
		.min = 0.0, .max = 1.0, .value = 0.0,
		.onChanged = [&](double v) { callCount++; lastVal = v; },
	});

	slider.setValue(0.5);
	EXPECT_EQ(callCount, 1);
	EXPECT_DOUBLE_EQ(lastVal, 0.5);
}

TEST_F(SliderTest, CallbackDoesNotFireWhenValueUnchanged) {
	int callCount = 0;

	Slider slider(Slider::Args{
		.min = 0.0, .max = 1.0, .value = 0.5,
		.onChanged = [&](double /*v*/) { callCount++; },
	});

	slider.setValue(0.5); // Same value
	EXPECT_EQ(callCount, 0);
}

// === Keyboard input ===

TEST_F(SliderTest, KeyboardArrowsAdjustValue) {
	Slider slider(Slider::Args{.min = 0.0, .max = 100.0, .value = 50.0});
	FocusManager::Get().registerFocusable(&slider, -1);

	slider.onFocusGained();
	double before = slider.getValue();
	slider.handleKeyInput(engine::Key::Right, false, false, false);
	EXPECT_GT(slider.getValue(), before);

	slider.handleKeyInput(engine::Key::Left, false, false, false);
	EXPECT_NEAR(slider.getValue(), before, 1e-9);
}

TEST_F(SliderTest, HomeAndEndKeys) {
	Slider slider(Slider::Args{.min = 10.0, .max = 90.0, .value = 50.0});

	slider.handleKeyInput(engine::Key::Home, false, false, false);
	EXPECT_DOUBLE_EQ(slider.getValue(), 10.0);

	slider.handleKeyInput(engine::Key::End, false, false, false);
	EXPECT_DOUBLE_EQ(slider.getValue(), 90.0);
}

TEST_F(SliderTest, DisabledSliderIgnoresKeyInput) {
	Slider slider(Slider::Args{
		.min = 0.0, .max = 100.0, .value = 50.0, .disabled = true,
	});

	slider.handleKeyInput(engine::Key::Home, false, false, false);
	EXPECT_DOUBLE_EQ(slider.getValue(), 50.0); // Unchanged
}

// === Focus ===

TEST_F(SliderTest, CanReceiveFocusWhenEnabled) {
	Slider slider(Slider::Args{.min = 0.0, .max = 1.0, .value = 0.5});
	EXPECT_TRUE(slider.canReceiveFocus());
}

TEST_F(SliderTest, CannotReceiveFocusWhenDisabled) {
	Slider slider(Slider::Args{.min = 0.0, .max = 1.0, .value = 0.5, .disabled = true});
	EXPECT_FALSE(slider.canReceiveFocus());
}

} // namespace UI
