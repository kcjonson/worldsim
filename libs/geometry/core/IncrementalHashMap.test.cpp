#include "core/IncrementalHashMap.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <map>
#include <random>

namespace {

	struct Weak {
		// A few keys share each home: probe runs, growth while draining, and deletion
		// across the two tables all happen.
		std::uint64_t operator()(std::int64_t k) const { return static_cast<std::uint64_t>(k / 3); }
	};

} // namespace

// Random inserts and erases against std::map: every lookup agrees, through many growths.
TEST(IncrementalHashMap, AgreesWithAMapThroughGrowth) {
	geometry::IncrementalHashMap<std::int64_t, std::int32_t, Weak> map;
	std::map<std::int64_t, std::int32_t>						   model;
	std::mt19937												   rng(3);
	for (int step = 0; step < 200000; ++step) {
		const std::int64_t key = static_cast<std::int64_t>(rng() % 50000);
		const std::int32_t*  got = map.find(key);
		const auto		   it  = model.find(key);
		ASSERT_EQ(got != nullptr, it != model.end()) << "step " << step;
		if (got != nullptr) {
			ASSERT_EQ(*got, it->second);
		}
		if (it == model.end() && rng() % 3 != 0) {
			map.insert(key, step);
			model[key] = step;
		} else if (it != model.end()) {
			ASSERT_TRUE(map.erase(key));
			model.erase(it);
		}
		ASSERT_EQ(map.size(), model.size());
	}
}
