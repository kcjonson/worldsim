#include "TileImageStrip.h"

#include <primitives/Primitives.h>
#include <theme/Tokens.h>
#include <ui/Context.h>

#include <algorithm>
#include <sstream>
#include <utility>

namespace asset_manager {

	namespace {

		constexpr float kGap = 10.0F;
		constexpr float kMinSlot = 56.0F; // a caption's room, under an image narrower than it
		constexpr float kCaptionGap = 4.0F;
		constexpr float kCaptionLine = 14.0F;
		constexpr float kCaptionScale = 11.0F / 16.0F;

	} // namespace

	TileImageStrip::TileImageStrip(const Args& /*args*/) {}

	void TileImageStrip::setImages(std::vector<Image> images) {
		shown.clear();
		float  x = 0.0F;
		float  tallest = 0.0F;
		size_t captionLines = 0;
		for (Image& image : images) {
			if (image.width <= 0 || image.height <= 0 || image.rgba.size() < static_cast<size_t>(image.width) * image.height * 4) {
				continue;
			}
			// drawImage takes premultiplied texels
			for (size_t i = 0; i + 3 < image.rgba.size(); i += 4) {
				const uint32_t alpha = image.rgba[i + 3];
				for (size_t c = 0; c < 3; ++c) {
					image.rgba[i + c] = static_cast<uint8_t>(((image.rgba[i + c] * alpha) + 127U) / 255U);
				}
			}
			Shown entry;
			entry.texture = Renderer::Primitives::createTexture(
				{.width = image.width,
				 .height = image.height,
				 .format = Renderer::TextureFormat::Rgba8,
				 .linear = false,
				 .pixels = image.rgba.data()}
			);
			entry.bounds = {x, 0.0F, static_cast<float>(image.width), static_cast<float>(image.height)};
			std::istringstream lines(image.caption);
			for (std::string line; std::getline(lines, line);) {
				entry.captionLines.push_back(line);
			}
			captionLines = std::max(captionLines, entry.captionLines.size());
			tallest = std::max(tallest, static_cast<float>(image.height));
			x += std::max(static_cast<float>(image.width), kMinSlot) + kGap;
			shown.push_back(std::move(entry));
		}
		const float width = shown.empty() ? 0.0F : x - kGap;
		const float height = tallest + (captionLines > 0 ? kCaptionGap + (kCaptionLine * static_cast<float>(captionLines)) : 0.0F);
		setSize(Foundation::Vec2{width, height});
	}

	void TileImageStrip::render() {
		const Foundation::Color caption = getContext()->getTheme().get(theme::tokens::theme::color::textDim);
		float					tallest = 0.0F;
		for (const Shown& entry : shown) {
			tallest = std::max(tallest, entry.bounds.height);
		}
		for (const Shown& entry : shown) {
			Renderer::Primitives::drawImage({
				.texture = entry.texture.handle(),
				.bounds = entry.bounds,
				.tint = Foundation::Color(1.0F, 1.0F, 1.0F, opacity()),
				.key = drawKey(),
			});
			for (size_t i = 0; i < entry.captionLines.size(); ++i) {
				Renderer::Primitives::drawText({
					.text = entry.captionLines[i],
					.position = {entry.bounds.x, tallest + kCaptionGap + (kCaptionLine * static_cast<float>(i))},
					.scale = kCaptionScale,
					.color = Foundation::Color(caption.r, caption.g, caption.b, caption.a * opacity()),
					.key = drawKey(),
				});
			}
		}
	}

} // namespace asset_manager
