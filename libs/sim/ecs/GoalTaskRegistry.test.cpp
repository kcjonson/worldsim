#include "GoalTaskRegistry.h"

#include <gtest/gtest.h>

namespace ecs::test {

	// A second session in the same process must hand out the same chain ids as the first.
	TEST(GoalTaskRegistryTest, ChainIdsRestartAfterClear) {
		auto& registry = GoalTaskRegistry::Get();
		registry.clear();

		const uint64_t first = registry.allocateChainId();
		const uint64_t second = registry.allocateChainId();
		EXPECT_NE(first, 0U);
		EXPECT_NE(first, second);

		registry.clear();
		EXPECT_EQ(registry.allocateChainId(), first);
		EXPECT_EQ(registry.allocateChainId(), second);
		registry.clear();
	}

} // namespace ecs::test
