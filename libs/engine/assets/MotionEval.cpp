#include "assets/MotionEval.h"

#include <cmath>

namespace engine::assets {
	namespace {
		constexpr float kTwoPi = 6.28318530717958647692F;

		float waveValue(MotionWave wave, float x) {
			// x is in cycles; one period = 1.0.
			const float t = x - std::floor(x); // wrap to [0,1)
			if (wave == MotionWave::Triangle) {
				// Triangle in [-1,1]: 0->0, 0.25->1, 0.5->0, 0.75->-1, 1->0.
				if (t < 0.25F) {
					return 4.0F * t;
				}
				if (t < 0.75F) {
					return 2.0F - 4.0F * t;
				}
				return 4.0F * t - 4.0F;
			}
			return std::sin(kTwoPi * t);
		}
	} // namespace

	void evaluateClip(const MotionClip& clip, float phase, std::unordered_map<std::string, PartTransform>& out) {
		for (const MotionDriver& drv : clip.drivers) {
			const float value = drv.amp * waveValue(drv.wave, drv.freq * phase + drv.phaseOffset);

			PartTransform& pt = out[drv.part];
			if (drv.hasPivot) {
				pt.pivot = drv.pivot; // all of a part's drivers share its joint node
			}
			switch (drv.channel) {
				case MotionChannel::Rotation:
					pt.rotation += value;
					break;
				case MotionChannel::PosX:
					pt.translate.x += value;
					break;
				case MotionChannel::PosY:
					pt.translate.y += value;
					break;
				case MotionChannel::ScaleX:
					pt.scale.x += value;
					break;
				case MotionChannel::ScaleY:
					pt.scale.y += value;
					break;
			}
		}
	}

} // namespace engine::assets
