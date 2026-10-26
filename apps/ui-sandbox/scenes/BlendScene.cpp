// Blend Scene - Alpha and Additive side by side over a dark half and a light half:
// overlapping primaries, glows (box shadows), text, arcs and strokes, a
// premultiplied sprite through drawImage, and shapes drawn into a RenderTarget
// and composited back with drawImage, next to the same shapes drawn directly.

#include "DesignCanvas.h"
#include "SceneTypes.h"
#include <graphics/Color.h>
#include <primitives/Primitives.h>
#include <resources/RenderTarget.h>
#include <scene/Scene.h>
#include <scene/SceneManager.h>
#include <utils/Log.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

namespace {

	constexpr const char* kSceneName = "blend";
	constexpr float		  kPi = std::numbers::pi_v<float>;
	constexpr int		  kSpriteSize = 128;
	constexpr float		  kCanvasWidth = 3072.0F;
	constexpr float		  kCanvasHeight = 1728.0F;
	constexpr float		  kHalf = kCanvasWidth * 0.5F;

	const Foundation::Color kDark(0.06F, 0.06F, 0.09F, 1.0F);
	const Foundation::Color kLight(0.9F, 0.9F, 0.87F, 1.0F);
	const Foundation::Color kInkOnDark(0.8F, 0.8F, 0.85F, 1.0F);
	const Foundation::Color kInkOnLight(0.2F, 0.2F, 0.25F, 1.0F);

	using Renderer::Primitives::BlendMode;

	class BlendScene : public engine::IScene {
	  public:
		void onEnter() override {
			// A soft warm glow, premultiplied: each texel's colour already times its alpha.
			std::vector<uint8_t> pixels(static_cast<size_t>(kSpriteSize) * kSpriteSize * 4);
			for (int y = 0; y < kSpriteSize; ++y) {
				for (int x = 0; x < kSpriteSize; ++x) {
					const float	 dx = ((static_cast<float>(x) + 0.5F) / kSpriteSize * 2.0F) - 1.0F;
					const float	 dy = ((static_cast<float>(y) + 0.5F) / kSpriteSize * 2.0F) - 1.0F;
					const float	 falloff = std::max(0.0F, 1.0F - std::sqrt((dx * dx) + (dy * dy)));
					const float	 alpha = falloff * falloff;
					const size_t i = ((static_cast<size_t>(y) * kSpriteSize) + static_cast<size_t>(x)) * 4;
					pixels[i + 0] = static_cast<uint8_t>(std::lround(255.0F * 1.0F * alpha));
					pixels[i + 1] = static_cast<uint8_t>(std::lround(255.0F * 0.75F * alpha));
					pixels[i + 2] = static_cast<uint8_t>(std::lround(255.0F * 0.35F * alpha));
					pixels[i + 3] = static_cast<uint8_t>(std::lround(255.0F * alpha));
				}
			}
			sprite = Renderer::Primitives::createTexture({.width = kSpriteSize, .height = kSpriteSize, .pixels = pixels.data()});
			LOG_INFO(UI, "Blend scene");
		}

		void update(float /*dt*/) override {}

		void render() override {
			namespace P = Renderer::Primitives;

			P::clear(kDark);
			const Foundation::Mat4 canvas = ui_sandbox::designCanvas(kCanvasWidth, kCanvasHeight);
			P::PushTransform(canvas);
			// The light half runs from the canvas's middle past the window's edges, whatever its aspect.
			P::drawRect({.bounds = {kHalf, -kCanvasHeight, kCanvasWidth * 2.0F, kCanvasHeight * 3.0F}, .style = {.fill = kLight}});
			P::drawText(
				{.text = "Alpha and Additive over dark and light",
				 .position = {80.0F, 40.0F},
				 .scale = 2.5F,
				 .color = {0.6F, 0.6F, 0.65F, 1.0F}}
			);

			for (int side = 0; side < 2; ++side) {
				const float				 x0 = static_cast<float>(side) * kHalf;
				const Foundation::Color& ink = inkFor(side);
				const auto				 label = [&](const std::string& text, float x, float y) {
					  P::drawText({.text = text, .position = {x0 + x, y}, .scale = 2.0F, .color = ink});
				};

				// Row 1: overlapping primaries.
				for (int mode = 0; mode < 2; ++mode) {
					const BlendMode						   blend = mode == 0 ? BlendMode::Alpha : BlendMode::Additive;
					const float							   cx = x0 + 280.0F + (static_cast<float>(mode) * 560.0F);
					const std::array<Foundation::Color, 3> primaries = {
						Foundation::Color(1.0F, 0.15F, 0.15F, 0.75F),
						Foundation::Color(0.15F, 1.0F, 0.15F, 0.75F),
						Foundation::Color(0.15F, 0.25F, 1.0F, 0.75F)
					};
					for (int i = 0; i < 3; ++i) {
						const float angle = (2.0F * kPi * static_cast<float>(i) / 3.0F) - (kPi * 0.5F);
						P::drawCircle(
							{.center = {cx + (60.0F * std::cos(angle)), 300.0F + (60.0F * std::sin(angle))},
							 .radius = 100.0F,
							 .style = {.fill = primaries[static_cast<size_t>(i)]},
							 .blend = blend}
						);
					}
					label(mode == 0 ? "alpha" : "additive", cx - x0 - 60.0F, 445.0F);
				}

				// Row 2: glow, text, ring and stroke, alpha then additive.
				for (int mode = 0; mode < 2; ++mode) {
					const BlendMode blend = mode == 0 ? BlendMode::Alpha : BlendMode::Additive;
					const float		bx = x0 + 80.0F + (static_cast<float>(mode) * 720.0F);
					P::drawRect(
						{.bounds = {bx + 40.0F, 560.0F, 200.0F, 90.0F},
						 .style =
							 {.fill = {0.2F, 0.75F, 0.95F, 0.85F},
							  .border = Foundation::BorderStyle{.color = {0.7F, 0.95F, 1.0F, 1.0F}, .width = 3.0F, .cornerRadius = 16.0F},
							  .boxShadow = Foundation::BoxShadow{.color = {0.2F, 0.75F, 1.0F, 0.9F}, .blur = 30.0F, .spread = 6.0F}},
						 .blend = blend}
					);
					P::drawText(
						{.text = mode == 0 ? "ALPHA TEXT" : "ADDITIVE TEXT",
						 .position = {bx + 20.0F, 700.0F},
						 .scale = 2.5F,
						 .color = {1.0F, 0.55F, 0.2F, 0.9F},
						 .blend = blend}
					);
					P::drawArc(
						{.center = {bx + 470.0F, 640.0F},
						 .innerRadius = 50.0F,
						 .outerRadius = 85.0F,
						 .startAngle = -kPi * 0.5F,
						 .sweep = kPi * 1.5F,
						 .color = {0.95F, 0.3F, 0.85F, 0.8F},
						 .blend = blend}
					);
					const std::array<Foundation::Vec2, 4> stroke = {
						Foundation::Vec2(bx + 30.0F, 820.0F),
						Foundation::Vec2(bx + 200.0F, 760.0F),
						Foundation::Vec2(bx + 370.0F, 830.0F),
						Foundation::Vec2(bx + 560.0F, 770.0F)
					};
					P::drawPolyline(
						{.points = stroke, .width = 18.0F, .color = {0.4F, 1.0F, 0.4F, 0.8F}, .cap = P::LineCap::Round, .blend = blend}
					);
				}
				label("glow, text, arc, polyline: alpha left, additive right", 80.0F, 875.0F);

				// Row 3: a premultiplied sprite through drawImage, alpha then additive, three overlapping.
				for (int mode = 0; mode < 2; ++mode) {
					const BlendMode blend = mode == 0 ? BlendMode::Alpha : BlendMode::Additive;
					const float		sx = x0 + 120.0F + (static_cast<float>(mode) * 380.0F);
					for (int i = 0; i < 3; ++i) {
						P::drawImage(
							{.texture = sprite,
							 .bounds = {sx + (static_cast<float>(i) * 70.0F), 950.0F + (static_cast<float>(i % 2) * 40.0F), 200.0F, 200.0F},
							 .blend = blend}
						);
					}
				}
				label("drawImage, premultiplied sprite: alpha, additive", 80.0F, 1225.0F);
			}

			// Row 4: the same shapes drawn directly (left of each half) and drawn into a
			// RenderTarget over transparent, then composited with drawImage (right).
			drawTargetShapes(0.0F);
			P::PopTransform();
			P::flush();
			if (ensureTarget()) {
				target.begin();
				P::clear(Foundation::Color::transparent());
				P::PushTransform(canvas);
				drawTargetShapes(420.0F);
				P::PopTransform();
				P::flush();
				target.end();
				int width = 0;
				int height = 0;
				P::getLogicalViewport(width, height);
				P::drawImage(
					{.texture = target.texture(),
					 .bounds = {0.0F, 0.0F, static_cast<float>(width), static_cast<float>(height)},
					 .uvMin = {0.0F, 1.0F},
					 .uvMax = {1.0F, 0.0F}}
				);
			}
			P::PushTransform(canvas);
			for (int side = 0; side < 2; ++side) {
				P::drawText(
					{.text = "direct, then through a RenderTarget and drawImage",
					 .position = {(static_cast<float>(side) * kHalf) + 80.0F, 1630.0F},
					 .scale = 2.0F,
					 .color = inkFor(side)}
				);
			}
			P::PopTransform();
		}

		void onExit() override {
			target = Renderer::RenderTarget();
			Renderer::Primitives::destroyTexture(sprite);
			sprite = {};
		}

		std::string exportState() override { // NOLINT(readability-convert-member-functions-to-static)
			return R"({"scene": "blend"})";
		}

		const char* getName() const override { return kSceneName; }

	  private:
		// Label ink on the dark half (side 0) and the light half (side 1).
		static const Foundation::Color& inkFor(int side) { return side == 0 ? kInkOnDark : kInkOnLight; }

		// A soft translucent glow, a translucent ring and text on both halves, `dx` across.
		static void drawTargetShapes(float dx) {
			namespace P = Renderer::Primitives;
			for (int side = 0; side < 2; ++side) {
				const float x = (static_cast<float>(side) * kHalf) + 120.0F + dx;
				P::drawRect(
					{.bounds = {x + 40.0F, 1360.0F, 220.0F, 110.0F},
					 .style = {
						 .fill = {0.95F, 0.4F, 0.3F, 0.6F},
						 .border = Foundation::BorderStyle{.color = {1.0F, 0.85F, 0.8F, 0.9F}, .width = 3.0F, .cornerRadius = 20.0F},
						 .boxShadow = Foundation::BoxShadow{.color = {1.0F, 0.4F, 0.2F, 0.7F}, .blur = 40.0F, .spread = 4.0F}
					 }}
				);
				P::drawArc(
					{.center = {x + 150.0F, 1415.0F}, .innerRadius = 70.0F, .outerRadius = 100.0F, .color = {0.3F, 0.6F, 1.0F, 0.5F}}
				);
				P::drawText({.text = "premultiplied", .position = {x + 60.0F, 1530.0F}, .scale = 1.75F, .color = {0.3F, 0.8F, 0.4F, 0.8F}});
			}
		}

		// A target the size of the window's backbuffer, so the logical projection covers it
		// exactly as it covers the window. False while there is no backbuffer (minimised).
		bool ensureTarget() {
			int width = 0;
			int height = 0;
			Renderer::Primitives::readBackbuffer({}, width, height);
			if (width <= 0 || height <= 0) {
				return false;
			}
			if (!target.isValid() || target.width() != width || target.height() != height) {
				target = Renderer::RenderTarget::create(width, height);
			}
			return target.isValid();
		}

		Renderer::TextureHandle sprite;
		Renderer::RenderTarget	target;
	};

} // anonymous namespace

namespace ui_sandbox::scenes {
	extern const ui_sandbox::SceneInfo Blend = {kSceneName, []() { return std::make_unique<BlendScene>(); }};
} // namespace ui_sandbox::scenes
