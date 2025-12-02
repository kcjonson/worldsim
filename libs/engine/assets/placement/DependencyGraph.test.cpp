#include "DependencyGraph.h"

#include <gtest/gtest.h>
#include <algorithm>

using namespace engine::assets;

// ============================================================================
// Basic Node Tests
// ============================================================================

TEST(DependencyGraphTests, EmptyGraph) {
	DependencyGraph graph;

	EXPECT_TRUE(graph.getNodes().empty());
	EXPECT_FALSE(graph.hasCycle());
}

TEST(DependencyGraphTests, AddSingleNode) {
	DependencyGraph graph;
	graph.addNode("A");

	EXPECT_EQ(graph.getNodes().size(), 1);
	EXPECT_TRUE(graph.getNodes().count("A") > 0);
}

TEST(DependencyGraphTests, AddMultipleNodes) {
	DependencyGraph graph;
	graph.addNode("A");
	graph.addNode("B");
	graph.addNode("C");

	EXPECT_EQ(graph.getNodes().size(), 3);
}

TEST(DependencyGraphTests, AddDuplicateNode) {
	DependencyGraph graph;
	graph.addNode("A");
	graph.addNode("A");

	EXPECT_EQ(graph.getNodes().size(), 1);
}

// ============================================================================
// Dependency Tests
// ============================================================================

TEST(DependencyGraphTests, AddDependency) {
	DependencyGraph graph;
	graph.addDependency("B", "A"); // B depends on A

	EXPECT_EQ(graph.getNodes().size(), 2);
	EXPECT_TRUE(graph.getNodes().count("A") > 0);
	EXPECT_TRUE(graph.getNodes().count("B") > 0);
}

TEST(DependencyGraphTests, GetDependencies) {
	DependencyGraph graph;
	graph.addDependency("B", "A"); // B depends on A
	graph.addDependency("C", "A"); // C depends on A

	auto bDeps = graph.getDependencies("B");
	auto cDeps = graph.getDependencies("C");
	auto aDeps = graph.getDependencies("A");

	EXPECT_EQ(bDeps.size(), 1);
	EXPECT_EQ(bDeps[0], "A");
	EXPECT_EQ(cDeps.size(), 1);
	EXPECT_EQ(cDeps[0], "A");
	EXPECT_TRUE(aDeps.empty());
}

TEST(DependencyGraphTests, GetDependenciesNonexistent) {
	DependencyGraph graph;
	graph.addNode("A");

	auto deps = graph.getDependencies("NonExistent");
	EXPECT_TRUE(deps.empty());
}

// ============================================================================
// Spawn Order (Topological Sort) Tests
// ============================================================================

TEST(DependencyGraphTests, SpawnOrderSingleNode) {
	DependencyGraph graph;
	graph.addNode("A");

	auto order = graph.getSpawnOrder();
	EXPECT_EQ(order.size(), 1);
	EXPECT_EQ(order[0], "A");
}

TEST(DependencyGraphTests, SpawnOrderLinearChain) {
	DependencyGraph graph;
	// C -> B -> A (C depends on B, B depends on A)
	graph.addDependency("C", "B");
	graph.addDependency("B", "A");

	auto order = graph.getSpawnOrder();
	EXPECT_EQ(order.size(), 3);

	// A must come before B, B must come before C
	auto posA = std::find(order.begin(), order.end(), "A") - order.begin();
	auto posB = std::find(order.begin(), order.end(), "B") - order.begin();
	auto posC = std::find(order.begin(), order.end(), "C") - order.begin();

	EXPECT_LT(posA, posB);
	EXPECT_LT(posB, posC);
}

TEST(DependencyGraphTests, SpawnOrderDiamondDependency) {
	DependencyGraph graph;
	// Diamond: D -> B -> A, D -> C -> A
	graph.addDependency("D", "B");
	graph.addDependency("D", "C");
	graph.addDependency("B", "A");
	graph.addDependency("C", "A");

	auto order = graph.getSpawnOrder();
	EXPECT_EQ(order.size(), 4);

	auto posA = std::find(order.begin(), order.end(), "A") - order.begin();
	auto posB = std::find(order.begin(), order.end(), "B") - order.begin();
	auto posC = std::find(order.begin(), order.end(), "C") - order.begin();
	auto posD = std::find(order.begin(), order.end(), "D") - order.begin();

	// A must come before B and C
	EXPECT_LT(posA, posB);
	EXPECT_LT(posA, posC);
	// B and C must come before D
	EXPECT_LT(posB, posD);
	EXPECT_LT(posC, posD);
}

TEST(DependencyGraphTests, SpawnOrderIndependentNodes) {
	DependencyGraph graph;
	graph.addNode("A");
	graph.addNode("B");
	graph.addNode("C");

	auto order = graph.getSpawnOrder();
	EXPECT_EQ(order.size(), 3);
	// All nodes should be present (order doesn't matter for independent nodes)
	EXPECT_NE(std::find(order.begin(), order.end(), "A"), order.end());
	EXPECT_NE(std::find(order.begin(), order.end(), "B"), order.end());
	EXPECT_NE(std::find(order.begin(), order.end(), "C"), order.end());
}

// ============================================================================
// Cycle Detection Tests
// ============================================================================

TEST(DependencyGraphTests, NoCycleLinear) {
	DependencyGraph graph;
	graph.addDependency("B", "A");
	graph.addDependency("C", "B");

	EXPECT_FALSE(graph.hasCycle());
}

TEST(DependencyGraphTests, NoCycleDiamond) {
	DependencyGraph graph;
	graph.addDependency("D", "B");
	graph.addDependency("D", "C");
	graph.addDependency("B", "A");
	graph.addDependency("C", "A");

	EXPECT_FALSE(graph.hasCycle());
}

TEST(DependencyGraphTests, SelfCycle) {
	DependencyGraph graph;
	graph.addDependency("A", "A"); // Self-loop

	EXPECT_TRUE(graph.hasCycle());
}

TEST(DependencyGraphTests, TwoNodeCycle) {
	DependencyGraph graph;
	graph.addDependency("A", "B");
	graph.addDependency("B", "A");

	EXPECT_TRUE(graph.hasCycle());
}

TEST(DependencyGraphTests, ThreeNodeCycle) {
	DependencyGraph graph;
	graph.addDependency("A", "B");
	graph.addDependency("B", "C");
	graph.addDependency("C", "A");

	EXPECT_TRUE(graph.hasCycle());
}

TEST(DependencyGraphTests, CycleInLargerGraph) {
	DependencyGraph graph;
	// Valid chain: D -> C -> B -> A
	graph.addDependency("D", "C");
	graph.addDependency("C", "B");
	graph.addDependency("B", "A");
	// Add cycle: A -> D
	graph.addDependency("A", "D");

	EXPECT_TRUE(graph.hasCycle());
}

TEST(DependencyGraphTests, GetSpawnOrderThrowsOnCycle) {
	DependencyGraph graph;
	graph.addDependency("A", "B");
	graph.addDependency("B", "A");

	EXPECT_THROW(graph.getSpawnOrder(), CyclicDependencyError);
}

// ============================================================================
// Clear Tests
// ============================================================================

TEST(DependencyGraphTests, ClearGraph) {
	DependencyGraph graph;
	graph.addDependency("B", "A");
	graph.addDependency("C", "B");

	EXPECT_EQ(graph.getNodes().size(), 3);

	graph.clear();

	EXPECT_TRUE(graph.getNodes().empty());
	EXPECT_FALSE(graph.hasCycle());
}

// ============================================================================
// Real-World Scenario Tests
// ============================================================================

TEST(DependencyGraphTests, FloraScenario) {
	DependencyGraph graph;
	// Trees must spawn before mushrooms (mushrooms grow near trees)
	// Flowers are independent
	graph.addNode("Oak");
	graph.addNode("Pine");
	graph.addNode("Flower");
	graph.addDependency("Mushroom", "Oak");
	graph.addDependency("Mushroom", "Pine");

	EXPECT_FALSE(graph.hasCycle());

	auto order = graph.getSpawnOrder();
	EXPECT_EQ(order.size(), 4);

	auto posOak = std::find(order.begin(), order.end(), "Oak") - order.begin();
	auto posPine = std::find(order.begin(), order.end(), "Pine") - order.begin();
	auto posMushroom = std::find(order.begin(), order.end(), "Mushroom") - order.begin();

	// Both trees must come before mushroom
	EXPECT_LT(posOak, posMushroom);
	EXPECT_LT(posPine, posMushroom);
}
