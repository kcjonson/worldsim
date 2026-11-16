#include "theme/InterfaceScale.h"

#include <gtest/gtest.h>

namespace theme {

	TEST(InterfaceScale, TheFactorRangeIsTheStopsAndTheDefaultIsOne) {
		const app::UiScaleConfig config = uiScaleConfig();
		EXPECT_FLOAT_EQ(config.minFactor, kInterfaceSizes.front());
		EXPECT_FLOAT_EQ(config.maxFactor, kInterfaceSizes.back());
		EXPECT_FLOAT_EQ(kInterfaceSizes.at(nearestInterfaceSize(kDefaultInterfaceSize)), kDefaultInterfaceSize);
	}

	TEST(InterfaceScale, ASavedFactorShowsAsTheNearestStop) {
		for (std::size_t i = 0; i < kInterfaceSizes.size(); ++i) {
			EXPECT_EQ(nearestInterfaceSize(kInterfaceSizes[i]), i);
		}
		EXPECT_FLOAT_EQ(kInterfaceSizes.at(nearestInterfaceSize(1.04F)), 1.0F);
		EXPECT_FLOAT_EQ(kInterfaceSizes.at(nearestInterfaceSize(1.07F)), 1.1F);
		EXPECT_EQ(nearestInterfaceSize(0.1F), 0U);
		EXPECT_EQ(nearestInterfaceSize(3.0F), kInterfaceSizes.size() - 1);
	}

} // namespace theme
