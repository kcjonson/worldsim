#include "nav/PatchableMeshHistory.test.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

using geometry::nav::testdata::runHistory;
using geometry::nav::testdata::StateHash;

// The whole mesh state after every patch of generated histories (limits, beams, lattice
// rectangles, obstacles, water, removals and moves), hashed and pinned. The hashes were taken
// from the patch's implementation before its setup read rings through edge boxes and kept
// boundary intervals in the registry, so a change that must leave the mesh as it was can
// show it does: same vertices, triangles, ids, covers, sources, labels, widths and epochs.
TEST(PatchableMeshHistory, PinnedStatesOverGeneratedHistories) {
	constexpr int								  kPatches = 24;
	constexpr std::array<std::uint64_t, 16> kPinned  = {
		 2558263201649449606ULL,  10897421179781705307ULL, 17635583441144116890ULL, 12659309427131412621ULL,
		 13734597629732120828ULL, 6792365952308999315ULL,  18056723438161817902ULL, 10728441683233452311ULL,
		 4576977803196823550ULL,  6536763196906006749ULL,  12940759280139982696ULL, 2654459439367740440ULL,
		 10866836184597550595ULL, 3622126027463736764ULL,  7691995182232726230ULL,	 15470348575450123497ULL};
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
