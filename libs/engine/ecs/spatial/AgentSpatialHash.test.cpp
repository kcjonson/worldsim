#include "AgentSpatialHash.h"

#include <gtest/gtest.h>

using namespace ecs;

TEST(AgentSpatialHash, EmptyQueryReturnsEmpty) {
    AgentSpatialHash hash(1.0f);
    std::vector<EntityID> out;
    hash.queryNeighbors({0.0f, 0.0f}, 1.0f, out);
    EXPECT_TRUE(out.empty());
}

TEST(AgentSpatialHash, InsertedAgentIsFoundInRadius) {
    AgentSpatialHash hash(1.0f);
    std::vector<EntityID> out;

    hash.insert(1u, {0.0f, 0.0f});
    hash.queryNeighbors({0.0f, 0.0f}, 1.0f, out);

    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0], 1u);
}

TEST(AgentSpatialHash, FarAgentIsNotReturned) {
    AgentSpatialHash hash(1.0f);
    std::vector<EntityID> out;

    hash.insert(1u, {0.0f, 0.0f});
    hash.insert(2u, {100.0f, 100.0f});

    // Query only around the origin.
    hash.queryNeighbors({0.0f, 0.0f}, 1.0f, out);

    // Agent 1 should be in range; agent 2 is in a far cell and must not appear.
    for (EntityID e : out) {
        EXPECT_NE(e, 2u);
    }
}

TEST(AgentSpatialHash, MultipleAgentsInRangeAllReturned) {
    AgentSpatialHash hash(1.0f);
    std::vector<EntityID> out;

    hash.insert(10u, {0.0f, 0.0f});
    hash.insert(11u, {0.3f, 0.0f});
    hash.insert(12u, {0.0f, 0.3f});
    hash.insert(99u, {50.0f, 50.0f}); // far — must not appear

    hash.queryNeighbors({0.0f, 0.0f}, 1.0f, out);

    bool found10 = false, found11 = false, found12 = false, found99 = false;
    for (EntityID e : out) {
        if (e == 10u) found10 = true;
        if (e == 11u) found11 = true;
        if (e == 12u) found12 = true;
        if (e == 99u) found99 = true;
    }
    EXPECT_TRUE(found10);
    EXPECT_TRUE(found11);
    EXPECT_TRUE(found12);
    EXPECT_FALSE(found99);
}

TEST(AgentSpatialHash, ClearAndRebuildIsConsistent) {
    AgentSpatialHash hash(1.0f);
    std::vector<EntityID> out;

    hash.insert(1u, {0.0f, 0.0f});
    hash.insert(2u, {0.1f, 0.0f});

    hash.clear();
    hash.queryNeighbors({0.0f, 0.0f}, 1.0f, out);
    EXPECT_TRUE(out.empty());

    // Re-insert only one agent; verify only it comes back.
    hash.insert(1u, {0.0f, 0.0f});
    hash.queryNeighbors({0.0f, 0.0f}, 1.0f, out);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0], 1u);
}
