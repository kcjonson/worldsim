#pragma once

// Interface scaling's numbers, which every app's main and any test App run through: a ui px is a pixel on a
// 1600 x 1000 screen (the prototype's stage), the scale never under 0.8, the player's interface size 60 to 120% in
// steps of 10, and a window of at least 1280 x 720 device pixels, so the canvas is at least 1600 x 900 ui px at the
// floor (1481 x 833 at the largest size). No frame: the play area takes whatever room the window has.

#include <DevServer/DevServer.h>
#include <application/AppConfig.h>

#include <array>
#include <cmath>
#include <cstddef>

namespace app {
	class App;
}

namespace theme {

	inline constexpr int kMinWindowWidth = 1280;
	inline constexpr int kMinWindowHeight = 720;

	// The player's factor on the scale, ui.scale, as the Settings scene offers it.
	inline constexpr std::array<float, 7> kInterfaceSizes{0.6F, 0.7F, 0.8F, 0.9F, 1.0F, 1.1F, 1.2F};
	inline constexpr float				  kDefaultInterfaceSize = 1.0F;

	[[nodiscard]] constexpr app::UiScaleConfig uiScaleConfig() {
		return {
			.referenceWidth = 1600,
			.referenceHeight = 1000,
			.floor = 0.8F,
			.minFactor = kInterfaceSizes.front(),
			.maxFactor = kInterfaceSizes.back(),
		};
	}

	// The stop in kInterfaceSizes nearest a factor, a value halfway between two going to the larger. The tolerance
	// absorbs the stops' float rounding, which makes 0.65 a tie as much as 0.75 is.
	[[nodiscard]] inline std::size_t nearestInterfaceSize(float factor) {
		constexpr float kTieTolerance = 1e-4F;
		std::size_t		nearest = 0;
		for (std::size_t i = 1; i < kInterfaceSizes.size(); ++i) {
			if (std::abs(kInterfaceSizes[i] - factor) <= std::abs(kInterfaceSizes[nearest] - factor) + kTieTolerance) {
				nearest = i;
			}
		}
		return nearest;
	}

	// /api/state?what=uiscale: the factor, and the scale over the window's logical px and over its framebuffer.
	// /api/ui/tree and /api/ui/lint are in ui px and /api/input in logical px, so a tree position times
	// layoutScale is where to point.
	[[nodiscard]] app::DevServer::Registration registerUiScaleView(app::App& owner);

} // namespace theme
