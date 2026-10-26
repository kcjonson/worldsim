// Arc Scene - Primitives::drawArc: rings with partial sweeps (progress rings and
// gauges), pies, hairline rings, negative sweeps, an arc under PushTransform, and
// the SDF disk next to drawCircle's 64-segment fan.

#include "DesignCanvas.h"
#include "SceneTypes.h"
#include <graphics/Color.h>
#include <primitives/Primitives.h>
#include <scene/Scene.h>
#include <scene/SceneManager.h>
#include <utils/Log.h>

#include <array>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include <numbers>
#include <string>

namespace {

	constexpr const char* kSceneName = "arc";
	constexpr float		  kPi = std::numbers::pi_v<float>;
	constexpr float		  kTop = -kPi * 0.5F; // Angles run clockwise on screen from +x, so the top is -90 degrees
	constexpr float		  kCanvasWidth = 2000.0F;
	constexpr float		  kCanvasHeight = 1380.0F;

	const Foundation::Color kBackground(0.08F, 0.08F, 0.11F, 1.0F);
	const Foundation::Color kLabel(0.75F, 0.75F, 0.8F, 1.0F);
	const Foundation::Color kTrack(1.0F, 1.0F, 1.0F, 0.12F);
	const Foundation::Color kValue(0.3F, 0.85F, 0.95F, 1.0F);

	// A label centred on `centreX`, its top at `top`.
	void label(const std::string& text, float centreX, float top) {
		Renderer::Primitives::drawText(
			{.text = text,
			 .position = {centreX - 300.0F, top},
			 .scale = 1.5F,
			 .color = kLabel,
			 .hAlign = Foundation::HorizontalAlign::Center,
			 .boxWidth = 600.0F}
		);
	}

	class ArcScene : public engine::IScene {
	  public:
		void onEnter() override { LOG_INFO(UI, "Arc scene"); }

		void update(float /*dt*/) override {}

		void render() override {
			namespace P = Renderer::Primitives;

			P::clear(kBackground);
			P::PushTransform(ui_sandbox::designCanvas(kCanvasWidth, kCanvasHeight));
			P::drawText(
				{.text = "drawArc: an SDF ring, arc or pie; angles clockwise on screen from +x",
				 .position = {60.0F, 30.0F},
				 .scale = 2.0F,
				 .color = Foundation::Color::white()}
			);

			// Row 1: progress rings, a dim full track under a partial sweep from the top.
			constexpr std::array<float, 6> kFractions = {0.0F, 0.1F, 0.25F, 0.5F, 0.75F, 1.0F};
			for (size_t i = 0; i < kFractions.size(); ++i) {
				const Foundation::Vec2 center(200.0F + (static_cast<float>(i) * 320.0F), 280.0F);
				P::drawArc({.center = center, .innerRadius = 95.0F, .outerRadius = 120.0F, .color = kTrack});
				P::drawArc(
					{.center = center,
					 .innerRadius = 95.0F,
					 .outerRadius = 120.0F,
					 .startAngle = kTop,
					 .sweep = 2.0F * kPi * kFractions[i],
					 .color = kValue}
				);
				label(std::to_string(static_cast<int>(kFractions[i] * 100.0F)) + "%", center.x, center.y - 14.0F);
			}
			label("a partial sweep from the top over a full track; 0% draws nothing", 1000.0F, 430.0F);

			// Row 2: a pie, hairlines, a negative sweep, a wide gauge, a sliver.
			P::drawArc(
				{.center = {200.0F, 700.0F},
				 .outerRadius = 120.0F,
				 .startAngle = kTop,
				 .sweep = kPi * 1.4F,
				 .color = {0.95F, 0.6F, 0.2F, 1.0F}}
			);
			label("pie, inner radius 0", 200.0F, 900.0F);
			P::drawArc({.center = {560.0F, 700.0F}, .innerRadius = 119.0F, .outerRadius = 120.0F, .color = Foundation::Color::white()});
			P::drawArc({.center = {560.0F, 700.0F}, .innerRadius = 98.0F, .outerRadius = 100.0F, .color = Foundation::Color::white()});
			P::drawArc({.center = {560.0F, 700.0F}, .innerRadius = 77.0F, .outerRadius = 80.0F, .color = Foundation::Color::white()});
			label("1, 2 and 3 unit rings", 560.0F, 900.0F);
			P::drawArc(
				{.center = {920.0F, 700.0F},
				 .innerRadius = 80.0F,
				 .outerRadius = 120.0F,
				 .startAngle = kTop,
				 .sweep = -kPi * 0.6F,
				 .color = {0.6F, 0.9F, 0.4F, 1.0F}}
			);
			label("negative sweep", 920.0F, 900.0F);
			P::drawArc(
				{.center = {1330.0F, 730.0F},
				 .innerRadius = 120.0F,
				 .outerRadius = 150.0F,
				 .startAngle = kPi * 0.75F,
				 .sweep = kPi * 1.5F,
				 .color = kTrack}
			);
			P::drawArc(
				{.center = {1330.0F, 730.0F},
				 .innerRadius = 120.0F,
				 .outerRadius = 150.0F,
				 .startAngle = kPi * 0.75F,
				 .sweep = kPi * 1.5F * 0.65F,
				 .color = {0.95F, 0.35F, 0.35F, 1.0F}}
			);
			label("270-degree gauge, 65%", 1330.0F, 900.0F);
			P::drawArc(
				{.center = {1760.0F, 700.0F},
				 .innerRadius = 60.0F,
				 .outerRadius = 120.0F,
				 .startAngle = 0.0F,
				 .sweep = kPi * 0.02F,
				 .color = kValue}
			);
			label("a 3.6-degree sliver", 1760.0F, 900.0F);

			// Row 3: transform, translucency, and the SDF disk against the fan.
			P::PushTransform(
				glm::scale(
					glm::rotate(
						glm::translate(Foundation::Mat4(1.0F), Foundation::Vec3(250.0F, 1130.0F, 0.0F)),
						kPi * 0.25F,
						Foundation::Vec3(0.0F, 0.0F, 1.0F)
					),
					Foundation::Vec3(1.5F, 1.5F, 1.0F)
				)
			);
			P::drawArc(
				{.center = {0.0F, 0.0F}, .innerRadius = 50.0F, .outerRadius = 80.0F, .startAngle = 0.0F, .sweep = kPi, .color = kValue}
			);
			P::PopTransform();
			label("PushTransform: rotate 45, scale 1.5", 250.0F, 1290.0F);
			for (int i = 0; i < 3; ++i) {
				const float offset = static_cast<float>(i) * 70.0F;
				P::drawArc(
					{.center = {700.0F + offset, 1130.0F},
					 .innerRadius = 40.0F,
					 .outerRadius = 100.0F,
					 .startAngle = kTop + (static_cast<float>(i) * 0.5F),
					 .sweep = kPi * 1.3F,
					 .color = {0.4F + (0.3F * static_cast<float>(i)), 0.5F, 1.0F - (0.3F * static_cast<float>(i)), 0.55F}}
				);
			}
			label("translucent, overlapping", 770.0F, 1290.0F);
			P::drawCircle({.center = {1200.0F, 1130.0F}, .radius = 110.0F, .style = {.fill = {0.85F, 0.85F, 0.9F, 1.0F}}});
			P::drawArc({.center = {1600.0F, 1130.0F}, .outerRadius = 110.0F, .color = {0.85F, 0.85F, 0.9F, 1.0F}});
			label("drawCircle's 64-segment fan", 1200.0F, 1290.0F);
			label("drawArc's SDF disk", 1600.0F, 1290.0F);
			P::PopTransform();
		}

		void onExit() override {}

		std::string exportState() override { // NOLINT(readability-convert-member-functions-to-static)
			return R"({"scene": "arc"})";
		}

		const char* getName() const override { return kSceneName; }
	};

} // anonymous namespace

namespace ui_sandbox::scenes {
	extern const ui_sandbox::SceneInfo Arc = {kSceneName, []() { return std::make_unique<ArcScene>(); }};
} // namespace ui_sandbox::scenes
