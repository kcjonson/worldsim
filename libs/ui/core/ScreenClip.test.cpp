#include "core/ScreenClip.h"

#include <primitives/Primitives.h>

#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>

#include <limits>
#include <numbers>

namespace UI {

	namespace {

		void expectRect(const Foundation::Rect& actual, const Foundation::Rect& expected) {
			constexpr float kTolerance = 1e-4F;
			EXPECT_NEAR(actual.x, expected.x, kTolerance);
			EXPECT_NEAR(actual.y, expected.y, kTolerance);
			EXPECT_NEAR(actual.width, expected.width, kTolerance);
			EXPECT_NEAR(actual.height, expected.height, kTolerance);
		}

		// Holds a transform pushed for one test.
		class ScopedTransform {
		  public:
			explicit ScopedTransform(const Foundation::Mat4& transform) { Renderer::Primitives::PushTransform(transform); }
			~ScopedTransform() { Renderer::Primitives::PopTransform(); }

			ScopedTransform(const ScopedTransform&) = delete;
			ScopedTransform& operator=(const ScopedTransform&) = delete;
			ScopedTransform(ScopedTransform&&) = delete;
			ScopedTransform& operator=(ScopedTransform&&) = delete;
		};

	} // namespace

	TEST(ScreenClipTest, UnderNoTransformTheRectIsItsOwnClip) {
		expectRect(screenClip({10.0F, 20.0F, 30.0F, 40.0F}), {10.0F, 20.0F, 30.0F, 40.0F});
	}

	TEST(ScreenClipTest, AnOffsetMovesTheClip) {
		const ScopedTransform offset(glm::translate(Foundation::Mat4(1.0F), Foundation::Vec3(100.0F, 50.0F, 0.0F)));
		expectRect(screenClip({10.0F, 20.0F, 30.0F, 40.0F}), {110.0F, 70.0F, 30.0F, 40.0F});
	}

	TEST(ScreenClipTest, ARotationClipsToTheBoundsOfAllFourCorners) {
		// A quarter turn takes (x, y) to (-y, x).
		const ScopedTransform turn(
			glm::rotate(Foundation::Mat4(1.0F), std::numbers::pi_v<float> * 0.5F, Foundation::Vec3(0.0F, 0.0F, 1.0F))
		);
		expectRect(screenClip({10.0F, 20.0F, 30.0F, 40.0F}), {-60.0F, 10.0F, 40.0F, 30.0F});
	}

	TEST(ScreenClipTest, ANegativeOrNanExtentIsTheEmptyClipNotAFlippedOne) {
		const ScopedTransform offset(glm::translate(Foundation::Mat4(1.0F), Foundation::Vec3(100.0F, 50.0F, 0.0F)));
		const float			  nan = std::numeric_limits<float>::quiet_NaN();
		for (const Foundation::Rect& local :
			 {Foundation::Rect{10.0F, 20.0F, -30.0F, 40.0F},
			  Foundation::Rect{10.0F, 20.0F, 30.0F, -40.0F},
			  Foundation::Rect{10.0F, 20.0F, nan, 40.0F},
			  Foundation::Rect{nan, 20.0F, 30.0F, 40.0F}}) {
			const Foundation::Rect clip = screenClip(local);
			EXPECT_EQ(clip.width * clip.height, 0.0F);
		}
	}

} // namespace UI
