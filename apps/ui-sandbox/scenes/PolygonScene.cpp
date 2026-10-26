// Polygon Scene - Primitives::drawPolygon: convex, concave and self-crossing fills,
// borders that sit outside the outline, one silhouette under several transforms
// (tessellated once, then cached), and additive fills.

#include "DesignCanvas.h"
#include "SceneTypes.h"
#include <graphics/Color.h>
#include <primitives/Primitives.h>
#include <scene/Scene.h>
#include <scene/SceneManager.h>
#include <utils/Log.h>

#include <array>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

namespace {

	constexpr const char* kSceneName = "polygon";
	constexpr float		  kPi = std::numbers::pi_v<float>;
	constexpr float		  kCanvasWidth = 3000.0F;
	constexpr float		  kCanvasHeight = 1580.0F;

	const Foundation::Color kBackground(0.08F, 0.08F, 0.11F, 1.0F);
	const Foundation::Color kLabel(0.75F, 0.75F, 0.8F, 1.0F);
	const Foundation::Color kRock(0.42F, 0.42F, 0.46F, 1.0F);
	const Foundation::Color kRim(0.85F, 0.9F, 1.0F, 1.0F);

	// A regular polygon or star around the origin: `points` corners, alternating
	// between `outer` and `inner` radius when they differ, starting at the top.
	std::vector<Foundation::Vec2> ring(int points, float outer, float inner) {
		std::vector<Foundation::Vec2> out;
		for (int i = 0; i < points; ++i) {
			const float radius = (i % 2 == 0) ? outer : inner;
			const float angle = (-kPi * 0.5F) + (2.0F * kPi * static_cast<float>(i) / static_cast<float>(points));
			out.emplace_back(radius * std::cos(angle), radius * std::sin(angle));
		}
		return out;
	}

	// An asteroid-like silhouette: an irregular, concave outline with a deep notch.
	std::vector<Foundation::Vec2> asteroid(float radius) {
		constexpr std::array<float, 11> kRadii = {1.0F, 0.86F, 0.95F, 0.78F, 0.5F, 0.83F, 1.0F, 0.9F, 0.72F, 0.97F, 0.88F};
		std::vector<Foundation::Vec2>	out;
		for (size_t i = 0; i < kRadii.size(); ++i) {
			const float angle = 2.0F * kPi * static_cast<float>(i) / static_cast<float>(kRadii.size());
			out.emplace_back(radius * kRadii[i] * std::cos(angle), radius * kRadii[i] * std::sin(angle));
		}
		return out;
	}

	// A pentagram: five points joined every second one, so its edges cross.
	std::vector<Foundation::Vec2> pentagram(float radius) {
		std::vector<Foundation::Vec2> out;
		for (int i = 0; i < 5; ++i) {
			const float angle = (-kPi * 0.5F) + (2.0F * kPi * static_cast<float>((i * 2) % 5) / 5.0F);
			out.emplace_back(radius * std::cos(angle), radius * std::sin(angle));
		}
		return out;
	}

	// A block with a 30-degree notch cut into its top, for the border's notch corner.
	std::vector<Foundation::Vec2> notched() {
		return {
			{-130.0F, -110.0F}, {-35.0F, -110.0F}, {0.0F, 20.0F}, {35.0F, -110.0F}, {130.0F, -110.0F}, {130.0F, 110.0F}, {-130.0F, 110.0F}
		};
	}

	Foundation::Mat4 placed(float x, float y, float radians = 0.0F, float scale = 1.0F) {
		Foundation::Mat4 m = glm::translate(Foundation::Mat4(1.0F), Foundation::Vec3(x, y, 0.0F));
		m = glm::rotate(m, radians, Foundation::Vec3(0.0F, 0.0F, 1.0F));
		return glm::scale(m, Foundation::Vec3(scale, scale, 1.0F));
	}

	// A label centred on `centreX`, its top at `top`.
	void label(const std::string& text, float centreX, float top) {
		Renderer::Primitives::drawText(
			{.text = text,
			 .position = {centreX - 300.0F, top},
			 .scale = 2.0F,
			 .color = kLabel,
			 .hAlign = Foundation::HorizontalAlign::Center,
			 .boxWidth = 600.0F}
		);
	}

	class PolygonScene : public engine::IScene {
	  public:
		void onEnter() override {
			hexagon = ring(6, 130.0F, 130.0F);
			star = ring(10, 140.0F, 60.0F);
			crossing = pentagram(140.0F);
			rock = asteroid(130.0F);
			square = ring(4, 110.0F, 110.0F);
			triangle = ring(3, 120.0F, 120.0F);
			notch = notched();
			LOG_INFO(UI, "Polygon scene");
		}

		void update(float /*dt*/) override {}

		void render() override {
			using Renderer::Primitives::BlendMode;
			namespace P = Renderer::Primitives;

			P::clear(kBackground);
			P::PushTransform(ui_sandbox::designCanvas(kCanvasWidth, kCanvasHeight));
			P::drawText(
				{.text = "drawPolygon: one tessellation per outline, border outside the fill",
				 .position = {80.0F, 40.0F},
				 .scale = 2.75F,
				 .color = Foundation::Color::white()}
			);

			// Row 1: the fill rule and the border.
			draw(hexagon, {.fill = {0.25F, 0.55F, 0.85F, 1.0F}}, placed(220.0F, 330.0F));
			label("convex", 220.0F, 495.0F);
			draw(star, {.fill = {0.95F, 0.75F, 0.2F, 1.0F}}, placed(590.0F, 330.0F));
			label("concave star", 590.0F, 495.0F);
			draw(crossing, {.fill = {0.85F, 0.35F, 0.35F, 1.0F}}, placed(960.0F, 330.0F));
			label("self-crossing", 960.0F, 495.0F);
			draw(rock, {.fill = kRock, .border = Foundation::LineStyle{.color = kRim, .width = 4.0F}}, placed(1330.0F, 330.0F));
			label("rim border", 1330.0F, 495.0F);
			draw(
				notch,
				{.fill = kRock, .border = Foundation::LineStyle{.color = {1.0F, 0.85F, 0.3F, 1.0F}, .width = 10.0F}},
				placed(1700.0F, 340.0F)
			);
			label("30-degree notch", 1700.0F, 495.0F);
			draw(
				square,
				{.fill = Foundation::Color::transparent(),
				 .border = Foundation::LineStyle{.color = {0.4F, 0.9F, 0.5F, 1.0F}, .width = 14.0F}},
				placed(2070.0F, 330.0F, kPi / 12.0F)
			);
			label("border only", 2070.0F, 495.0F);
			draw(
				star,
				{.fill = {0.2F, 0.2F, 0.25F, 1.0F}, .border = Foundation::LineStyle{.color = {1.0F, 0.55F, 0.2F, 1.0F}, .width = 8.0F}},
				placed(2430.0F, 330.0F)
			);
			label("tips rounded", 2430.0F, 495.0F);
			draw(
				hexagon,
				{.fill = {0.55F, 0.35F, 0.75F, 0.5F}, .border = Foundation::LineStyle{.color = {0.9F, 0.8F, 1.0F, 0.6F}, .width = 6.0F}},
				placed(2790.0F, 330.0F)
			);
			label("translucent", 2790.0F, 495.0F);

			// Row 2: one outline, five transforms, one tessellation.
			for (int i = 0; i < 5; ++i) {
				const float t = static_cast<float>(i);
				draw(
					rock,
					{.fill = kRock, .border = Foundation::LineStyle{.color = kRim, .width = 3.0F}},
					placed(300.0F + (t * 400.0F), 820.0F, t * 0.6F, 0.6F + (t * 0.2F))
				);
			}
			label("one silhouette, five PushTransforms", 1100.0F, 1040.0F);

			// Row 3: alpha and additive fills overlapping.
			const std::array<Foundation::Color, 3> primaries = {
				Foundation::Color(1.0F, 0.2F, 0.2F, 0.8F),
				Foundation::Color(0.2F, 1.0F, 0.2F, 0.8F),
				Foundation::Color(0.2F, 0.3F, 1.0F, 0.8F)
			};
			for (int i = 0; i < 3; ++i) {
				const float angle = (2.0F * kPi * static_cast<float>(i) / 3.0F) - (kPi * 0.5F);
				const float dx = 70.0F * std::cos(angle);
				const float dy = 70.0F * std::sin(angle);
				draw(triangle, {.fill = primaries[static_cast<size_t>(i)]}, placed(500.0F + dx, 1330.0F + dy), BlendMode::Alpha);
				draw(triangle, {.fill = primaries[static_cast<size_t>(i)]}, placed(1100.0F + dx, 1330.0F + dy), BlendMode::Additive);
			}
			label("alpha", 500.0F, 1500.0F);
			label("additive", 1100.0F, 1500.0F);
			draw(
				rock,
				{.fill = {0.25F, 0.45F, 0.9F, 0.7F}, .border = Foundation::LineStyle{.color = {0.5F, 0.8F, 1.0F, 1.0F}, .width = 6.0F}},
				placed(1700.0F, 1330.0F, 0.3F),
				BlendMode::Additive
			);
			label("additive fill and border", 1700.0F, 1500.0F);
			P::PopTransform();
		}

		void onExit() override {}

		std::string exportState() override { // NOLINT(readability-convert-member-functions-to-static)
			return R"({"scene": "polygon"})";
		}

		const char* getName() const override { return kSceneName; }

	  private:
		static void draw(
			const std::vector<Foundation::Vec2>& outline,
			const Foundation::PolygonStyle&		 style,
			const Foundation::Mat4&				 transform,
			Renderer::Primitives::BlendMode		 blend = Renderer::Primitives::BlendMode::Alpha
		) {
			Renderer::Primitives::PushTransform(transform);
			Renderer::Primitives::drawPolygon({.vertices = outline, .style = style, .blend = blend});
			Renderer::Primitives::PopTransform();
		}

		std::vector<Foundation::Vec2> hexagon;
		std::vector<Foundation::Vec2> star;
		std::vector<Foundation::Vec2> crossing;
		std::vector<Foundation::Vec2> rock;
		std::vector<Foundation::Vec2> square;
		std::vector<Foundation::Vec2> triangle;
		std::vector<Foundation::Vec2> notch;
	};

} // anonymous namespace

namespace ui_sandbox::scenes {
	extern const ui_sandbox::SceneInfo Polygon = {kSceneName, []() { return std::make_unique<PolygonScene>(); }};
} // namespace ui_sandbox::scenes
