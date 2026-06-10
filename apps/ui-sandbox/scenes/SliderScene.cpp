// Slider Scene - Slider Component Demo
// Shows linear, log-scale, and stepped sliders with live value display.

#include <GL/glew.h>

#include "SceneTypes.h"
#include <components/slider/Slider.h>
#include <input/InputEvent.h>
#include <primitives/Primitives.h>
#include <scene/Scene.h>
#include <scene/SceneManager.h>
#include <shapes/Shapes.h>

#include <format>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr const char* kSceneName = "slider";

class SliderScene : public engine::IScene {
  public:
	const char* getName() const override { return kSceneName; }
	std::string exportState() override { return R"({"scene":"slider"})"; }

	void onEnter() override {
		using namespace UI;

		constexpr float kX = 80.0F;
		constexpr float kSliderW = 360.0F;
		constexpr float kSliderH = 36.0F;
		float y = 80.0F;

		title = Text(Text::Args{
			.position = {kX, 30.0F},
			.text = "Slider Component Demo",
			.style = {.color = Foundation::Color::white(), .fontSize = 22.0F},
			.id = "title",
		});

		// 1. Linear 0-100
		addDescription(kX, y, "Linear slider (0-100%)");
		y += 20.0F;
		sliders.push_back(std::make_unique<Slider>(Slider::Args{
			.position = {kX, y},
			.size = {kSliderW, kSliderH},
			.min = 0.0, .max = 100.0, .step = 0.0, .value = 50.0,
			.label = "Water %",
			.valueFormatter = [](double v) { return std::format("{:.1f}%", v); },
			.onChanged = [this](double v) { values[0] = v; },
			.id = "slider_linear",
		}));
		values.push_back(50.0);
		y += kSliderH + 40.0F;

		// 2. Log scale 0.1-50
		addDescription(kX, y, "Log scale slider (0.1-50, e.g. star mass)");
		y += 20.0F;
		sliders.push_back(std::make_unique<Slider>(Slider::Args{
			.position = {kX, y},
			.size = {kSliderW, kSliderH},
			.min = 0.1, .max = 50.0, .step = 0.0, .value = 1.0,
			.logScale = true,
			.label = "Star mass",
			.valueFormatter = [](double v) { return std::format("{:.3f} M☉", v); },
			.onChanged = [this](double v) { values[1] = v; },
			.id = "slider_log",
		}));
		values.push_back(1.0);
		y += kSliderH + 40.0F;

		// 3. Stepped integer 2-30
		addDescription(kX, y, "Stepped slider (2-30 integer, tectonic plates)");
		y += 20.0F;
		sliders.push_back(std::make_unique<Slider>(Slider::Args{
			.position = {kX, y},
			.size = {kSliderW, kSliderH},
			.min = 2.0, .max = 30.0, .step = 1.0, .value = 12.0,
			.label = "Plates",
			.valueFormatter = [](double v) { return std::format("{:.0f}", v); },
			.onChanged = [this](double v) { values[2] = v; },
			.id = "slider_stepped",
		}));
		values.push_back(12.0);
		y += kSliderH + 40.0F;

		// 4. Disabled
		addDescription(kX, y, "Disabled slider");
		y += 20.0F;
		sliders.push_back(std::make_unique<Slider>(Slider::Args{
			.position = {kX, y},
			.size = {kSliderW, kSliderH},
			.min = 0.0, .max = 1.0, .value = 0.3,
			.label = "Disabled",
			.id = "slider_disabled",
			.disabled = true,
		}));
		values.push_back(0.3);
	}

	void onExit() override {
		sliders.clear();
		descriptions.clear();
		values.clear();
	}

	bool handleInput(UI::InputEvent& event) override {
		for (auto& slider : sliders) {
			if (slider->handleEvent(event)) return true;
		}
		return false;
	}

	void update(float dt) override {
		for (auto& slider : sliders) {
			slider->update(dt);
		}
	}

	void render() override {
		glClearColor(0.10F, 0.10F, 0.13F, 1.0F);
		glClear(GL_COLOR_BUFFER_BIT);

		title.render();

		for (auto& desc : descriptions) {
			desc.render();
		}
		for (size_t i = 0; i < sliders.size(); ++i) {
			sliders[i]->render();
		}

		// Live value display
		UI::Text hint(UI::Text::Args{
			.position = {500.0F, 80.0F},
			.text = "Values (live):",
			.style = {.color = Foundation::Color{0.7F, 0.7F, 0.7F, 1.0F}, .fontSize = 14.0F},
		});
		hint.render();

		const char* names[] = {"Linear", "Log", "Stepped", "Disabled"};
		float vy = 105.0F;
		for (size_t i = 0; i < values.size(); ++i) {
			std::string s = std::format("{}: {:.4f}", names[i], values[i]);
			UI::Text t(UI::Text::Args{
				.position = {500.0F, vy},
				.text = s,
				.style = {.color = Foundation::Color{0.6F, 0.9F, 0.6F, 1.0F}, .fontSize = 14.0F},
			});
			t.render();
			vy += 24.0F;
		}

		UI::Text keysHint(UI::Text::Args{
			.position = {500.0F, vy + 20.0F},
			.text = "Tab to focus, Left/Right arrows, Home/End",
			.style = {.color = Foundation::Color{0.5F, 0.5F, 0.5F, 1.0F}, .fontSize = 12.0F},
		});
		keysHint.render();
	}

  private:
	void addDescription(float x, float y, const std::string& text) {
		descriptions.push_back(UI::Text(UI::Text::Args{
			.position = {x, y},
			.text = text,
			.style = {.color = Foundation::Color{0.6F, 0.6F, 0.6F, 1.0F}, .fontSize = 13.0F},
		}));
	}

	UI::Text							  title;
	std::vector<UI::Text>				  descriptions;
	std::vector<std::unique_ptr<UI::Slider>> sliders;
	std::vector<double>					  values;
};

} // namespace

namespace ui_sandbox::scenes {
	extern const ui_sandbox::SceneInfo Slider = {kSceneName, []() {
		return std::make_unique<SliderScene>();
	}};
}
