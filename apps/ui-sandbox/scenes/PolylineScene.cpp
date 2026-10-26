// Polyline Scene - Primitives::drawPolyline: the three caps, round and mitered
// joins, closed outlines, hairlines, a dense curve, and translucent and additive
// strokes (whose overlapping joints show).

#include "SceneTypes.h"
#include <graphics/Color.h>
#include <primitives/Primitives.h>
#include <scene/Scene.h>
#include <scene/SceneManager.h>
#include <utils/Log.h>

#include <array>
#include <cmath>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

namespace {

	constexpr const char* kSceneName = "polyline";
	constexpr float		  kPi = std::numbers::pi_v<float>;

	const Foundation::Color kBackground(0.08F, 0.08F, 0.11F, 1.0F);
	const Foundation::Color kLabel(0.75F, 0.75F, 0.8F, 1.0F);
	const Foundation::Color kGuide(1.0F, 0.3F, 0.3F, 1.0F);

	void label(const std::string& text, float x, float y) {
		Renderer::Primitives::drawText({.text = text, .position = {x, y}, .scale = 1.25F, .color = kLabel});
	}

	// A zigzag of `count` points from (x, y), `step` apart, alternating `rise` up.
	std::vector<Foundation::Vec2> zigzag(float x, float y, int count, float step, float rise) {
		std::vector<Foundation::Vec2> out;
		for (int i = 0; i < count; ++i) {
			out.emplace_back(x + (static_cast<float>(i) * step), (i % 2 == 0) ? y : y - rise);
		}
		return out;
	}

	std::vector<Foundation::Vec2> regular(float x, float y, int points, float outer, float inner) {
		std::vector<Foundation::Vec2> out;
		for (int i = 0; i < points; ++i) {
			const float radius = (i % 2 == 0) ? outer : inner;
			const float angle = (-kPi * 0.5F) + (2.0F * kPi * static_cast<float>(i) / static_cast<float>(points));
			out.emplace_back(x + (radius * std::cos(angle)), y + (radius * std::sin(angle)));
		}
		return out;
	}

	class PolylineScene : public engine::IScene {
	  public:
		void onEnter() override {
			using Renderer::Primitives::LineCap;
			using Renderer::Primitives::LineJoin;
			for (int i = 0; i < 3; ++i) {
				capLines[static_cast<size_t>(i)] = zigzag(150.0F + (static_cast<float>(i) * 600.0F), 380.0F, 5, 110.0F, 160.0F);
			}
			for (int i = 0; i < 2; ++i) {
				joinLines[static_cast<size_t>(i)] = zigzag(1950.0F + (static_cast<float>(i) * 520.0F), 360.0F, 4, 150.0F, 100.0F);
			}
			hexagon = regular(250.0F, 800.0F, 6, 130.0F, 130.0F);
			star = regular(650.0F, 800.0F, 10, 140.0F, 60.0F);
			triangle = regular(1050.0F, 820.0F, 3, 140.0F, 140.0F);
			for (int i = 0; i <= 200; ++i) {
				const float t = static_cast<float>(i) / 200.0F;
				wave.emplace_back(1350.0F + (t * 1500.0F), 800.0F + (110.0F * std::sin(t * 6.0F * kPi) * (1.0F - (0.6F * t))));
			}
			for (int w = 0; w < 3; ++w) {
				std::vector<Foundation::Vec2> line;
				for (int i = 0; i <= 60; ++i) {
					const float t = static_cast<float>(i) / 60.0F;
					line.emplace_back(
						150.0F + (t * 700.0F), 1150.0F + (static_cast<float>(w) * 70.0F) + (25.0F * std::sin(t * 4.0F * kPi))
					);
				}
				hairlines[static_cast<size_t>(w)] = std::move(line);
			}
			translucent = zigzag(1050.0F, 1330.0F, 6, 110.0F, 180.0F);
			additive = zigzag(1800.0F, 1330.0F, 6, 110.0F, 180.0F);
			LOG_INFO(UI, "Polyline scene");
		}

		void update(float /*dt*/) override {}

		void render() override {
			namespace P = Renderer::Primitives;
			using P::LineCap;
			using P::LineJoin;

			P::clear(kBackground);
			P::drawText(
				{.text = "drawPolyline: SDF quads per segment, joint and round cap",
				 .position = {80.0F, 50.0F},
				 .scale = 2.0F,
				 .color = Foundation::Color::white()}
			);

			// Row 1: caps (round joins), then joins (butt caps) at turns under a right angle, where
			// a miter stays sharp. A thin guide shows the points.
			constexpr std::array<LineCap, 3>	 kCaps = {LineCap::Butt, LineCap::Round, LineCap::Square};
			constexpr std::array<const char*, 3> kCapNames = {"butt cap", "round cap", "square cap"};
			for (size_t i = 0; i < kCaps.size(); ++i) {
				P::drawPolyline({.points = capLines[i], .width = 36.0F, .color = {0.3F, 0.6F, 0.95F, 1.0F}, .cap = kCaps[i]});
				P::drawPolyline({.points = capLines[i], .width = 2.0F, .color = kGuide});
				label(kCapNames[i], capLines[i].front().x + 150.0F, 470.0F);
			}
			constexpr std::array<LineJoin, 2>	 kJoins = {LineJoin::Round, LineJoin::Miter};
			constexpr std::array<const char*, 2> kJoinNames = {"round join", "miter join"};
			for (size_t i = 0; i < kJoins.size(); ++i) {
				P::drawPolyline({.points = joinLines[i], .width = 36.0F, .color = {0.95F, 0.7F, 0.25F, 1.0F}, .join = kJoins[i]});
				P::drawPolyline({.points = joinLines[i], .width = 2.0F, .color = kGuide});
				label(kJoinNames[i], joinLines[i].front().x + 100.0F, 470.0F);
			}

			// Row 2: closed outlines and a dense curve.
			P::drawPolyline(
				{.points = hexagon, .width = 16.0F, .color = {0.4F, 0.9F, 0.5F, 1.0F}, .join = LineJoin::Miter, .closed = true}
			);
			label("closed, miter (60-degree turns)", 110.0F, 980.0F);
			P::drawPolyline(
				{.points = star, .width = 10.0F, .color = {0.95F, 0.45F, 0.45F, 1.0F}, .join = LineJoin::Miter, .closed = true}
			);
			label("star: tips past 90 degrees round", 520.0F, 980.0F);
			P::drawPolyline(
				{.points = triangle, .width = 14.0F, .color = {0.8F, 0.6F, 1.0F, 1.0F}, .join = LineJoin::Round, .closed = true}
			);
			label("closed, round", 990.0F, 980.0F);
			P::drawPolyline({.points = wave, .width = 6.0F, .color = {0.3F, 0.85F, 0.95F, 1.0F}, .cap = LineCap::Round});
			label("201 points, width 6, round caps", 1900.0F, 980.0F);

			// Row 3: hairlines, translucent and additive strokes.
			constexpr std::array<float, 3> kHairlines = {1.0F, 2.0F, 3.0F};
			for (size_t i = 0; i < kHairlines.size(); ++i) {
				P::drawPolyline({.points = hairlines[i], .width = kHairlines[i], .color = Foundation::Color::white()});
			}
			label("widths 1, 2 and 3", 360.0F, 1360.0F);
			P::drawPolyline({.points = translucent, .width = 40.0F, .color = {0.3F, 0.6F, 0.95F, 0.5F}, .cap = LineCap::Round});
			label("alpha 0.5: joints overlap", 1150.0F, 1560.0F);
			P::drawPolyline(
				{.points = additive,
				 .width = 40.0F,
				 .color = {0.95F, 0.5F, 0.2F, 0.6F},
				 .cap = LineCap::Round,
				 .join = LineJoin::Miter,
				 .blend = P::BlendMode::Additive}
			);
			label("additive, miter", 1950.0F, 1560.0F);
		}

		void onExit() override {}

		std::string exportState() override { // NOLINT(readability-convert-member-functions-to-static)
			return R"({"scene": "polyline"})";
		}

		const char* getName() const override { return kSceneName; }

	  private:
		std::array<std::vector<Foundation::Vec2>, 3> capLines;
		std::array<std::vector<Foundation::Vec2>, 2> joinLines;
		std::vector<Foundation::Vec2>				 hexagon;
		std::vector<Foundation::Vec2>				 star;
		std::vector<Foundation::Vec2>				 triangle;
		std::vector<Foundation::Vec2>				 wave;
		std::array<std::vector<Foundation::Vec2>, 3> hairlines;
		std::vector<Foundation::Vec2>				 translucent;
		std::vector<Foundation::Vec2>				 additive;
	};

} // anonymous namespace

namespace ui_sandbox::scenes {
	extern const ui_sandbox::SceneInfo Polyline = {kSceneName, []() { return std::make_unique<PolylineScene>(); }};
} // namespace ui_sandbox::scenes
