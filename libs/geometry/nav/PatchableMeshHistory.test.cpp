#include "nav/PatchableMeshHistory.test.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>

using geometry::nav::testdata::runHistory;
using geometry::nav::testdata::StateHash;

// The whole mesh state after every patch of generated histories (limits, beams, rectangles,
// rings of 17 to 70 corners, sight-sized pieces, obstacles, water, removals and moves),
// hashed and pinned. The hashes were taken from the patch's implementation before its setup
// read rings through edge boxes and kept boundary intervals in the registry, so a change that
// must leave the mesh as it was can show it does: same vertices, triangles, ids, covers,
// sources, labels, widths and epochs.
TEST(PatchableMeshHistory, PinnedStatesOverGeneratedHistories) {
	constexpr int							kPatches = 12;
	constexpr std::array<std::uint64_t, 16> kPinned	 = {
		 3435022494904696145ULL,  16716922973612902048ULL, 4066830524249299495ULL,	4450564347105521944ULL,
		 4338841432260652129ULL,  13547027957046119817ULL, 11383679595685753547ULL, 7486325161803599202ULL,
		 12246625684025157774ULL, 4351018589355161883ULL,  9600383739377199712ULL,	10688907723658036790ULL,
		 15735311822124273062ULL, 13312036051301350891ULL, 5337792350773027407ULL,	1525971169925490728ULL};
	for (std::uint32_t seed = 1; seed <= kPinned.size(); ++seed) {
		StateHash history;
		int		  patch = 0;
		runHistory(seed, kPatches, [&](std::uint64_t state, const std::string& valid, const geometry::nav::PatchableMesh&, const std::vector<geometry::nav::RingId>&) {
			EXPECT_EQ(valid, "") << "seed " << seed << " patch " << patch;
			history.add(static_cast<std::int64_t>(state));
			++patch;
		});
		EXPECT_EQ(history.value, kPinned[seed - 1]) << "seed " << seed;
	}
}
