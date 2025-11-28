#include "layer/layer.h"
#include <gtest/gtest.h>

using namespace UI;

// ============================================================================
// LayerHandle Tests
// ============================================================================

TEST(LayerHandleTest, DefaultConstructorIsInvalid) {
	LayerHandle handle;
	EXPECT_FALSE(handle.IsValid());
	EXPECT_EQ(handle.value, LayerHandle::kInvalidHandle);
}

TEST(LayerHandleTest, InvalidFactoryMethod) {
	LayerHandle handle = LayerHandle::Invalid();
	EXPECT_FALSE(handle.IsValid());
	EXPECT_EQ(handle.value, LayerHandle::kInvalidHandle);
}

TEST(LayerHandleTest, MakeCreatesValidHandle) {
	LayerHandle handle = LayerHandle::Make(42, 7);
	EXPECT_TRUE(handle.IsValid());
}

TEST(LayerHandleTest, GetIndexExtractsLowerBits) {
	LayerHandle handle = LayerHandle::Make(42, 7);
	EXPECT_EQ(handle.GetIndex(), 42);
}

TEST(LayerHandleTest, GetGenerationExtractsUpperBits) {
	LayerHandle handle = LayerHandle::Make(42, 7);
	EXPECT_EQ(handle.GetGeneration(), 7);
}

TEST(LayerHandleTest, MaxIndexValue) {
	LayerHandle handle = LayerHandle::Make(0xFFFF, 0);
	EXPECT_EQ(handle.GetIndex(), 0xFFFF);
	EXPECT_EQ(handle.GetGeneration(), 0);
	EXPECT_TRUE(handle.IsValid());
}

TEST(LayerHandleTest, MaxGenerationValue) {
	LayerHandle handle = LayerHandle::Make(0, 0xFFFE); // 0xFFFF would make it invalid
	EXPECT_EQ(handle.GetIndex(), 0);
	EXPECT_EQ(handle.GetGeneration(), 0xFFFE);
	EXPECT_TRUE(handle.IsValid());
}

TEST(LayerHandleTest, ZeroIndexZeroGeneration) {
	LayerHandle handle = LayerHandle::Make(0, 0);
	EXPECT_EQ(handle.GetIndex(), 0);
	EXPECT_EQ(handle.GetGeneration(), 0);
	EXPECT_TRUE(handle.IsValid());
}

TEST(LayerHandleTest, EqualityOperator) {
	LayerHandle a = LayerHandle::Make(10, 5);
	LayerHandle b = LayerHandle::Make(10, 5);
	LayerHandle c = LayerHandle::Make(10, 6);
	LayerHandle d = LayerHandle::Make(11, 5);

	EXPECT_EQ(a, b);
	EXPECT_NE(a, c);
	EXPECT_NE(a, d);
}

TEST(LayerHandleTest, InequalityOperator) {
	LayerHandle a = LayerHandle::Make(10, 5);
	LayerHandle b = LayerHandle::Make(10, 6);

	EXPECT_TRUE(a != b);
	EXPECT_FALSE(a != a);
}

TEST(LayerHandleTest, InvalidHandlesAreEqual) {
	LayerHandle a;
	LayerHandle b = LayerHandle::Invalid();

	EXPECT_EQ(a, b);
}
