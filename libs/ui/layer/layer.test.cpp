#include "layer/layer.h"
#include "components/button/button.h"
#include "components/text_input/text_input.h"
#include "shapes/shapes.h"
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

// ============================================================================
// Layer Concept Tests - Compile-time verification via static_assert
// ============================================================================

// These are compile-time checks - if they fail, the code won't compile.
// We also include runtime tests to ensure the static_asserts are present.

TEST(LayerConceptTest, ShapesSatisfyLayerConcept) {
	// These static_asserts are in shapes.h, but we verify they compile here
	EXPECT_TRUE(Layer<Container>);
	EXPECT_TRUE(Layer<Rectangle>);
	EXPECT_TRUE(Layer<Circle>);
	EXPECT_TRUE(Layer<Line>);
	EXPECT_TRUE(Layer<Text>);
}

TEST(LayerConceptTest, ComponentsSatisfyLayerConcept) {
	// These static_asserts are in button.h and text_input.h
	EXPECT_TRUE(Layer<Button>);
	EXPECT_TRUE(Layer<TextInput>);
}

// ============================================================================
// Focusable Concept Tests
// ============================================================================

TEST(FocusableConceptTest, ComponentsSatisfyFocusableConcept) {
	// Button and TextInput implement IFocusable and satisfy Focusable concept
	EXPECT_TRUE(Focusable<Button>);
	EXPECT_TRUE(Focusable<TextInput>);
}

TEST(FocusableConceptTest, ShapesDoNotSatisfyFocusableConcept) {
	// Shapes don't have focus methods - they don't satisfy Focusable
	EXPECT_FALSE(Focusable<Container>);
	EXPECT_FALSE(Focusable<Rectangle>);
	EXPECT_FALSE(Focusable<Circle>);
	EXPECT_FALSE(Focusable<Line>);
	EXPECT_FALSE(Focusable<Text>);
}

// ============================================================================
// Mock types to verify concept requirements
// ============================================================================

namespace {

// Type missing HandleInput - should NOT satisfy Layer
struct MissingHandleInput {
	void Update(float) {}
	void Render() const {}
};

// Type missing Update - should NOT satisfy Layer
struct MissingUpdate {
	void HandleInput() {}
	void Render() const {}
};

// Type missing Render - should NOT satisfy Layer
struct MissingRender {
	void HandleInput() {}
	void Update(float) {}
};

// Type with wrong return type - should NOT satisfy Layer
struct WrongReturnType {
	int  HandleInput() { return 0; }
	void Update(float) {}
	void Render() const {}
};

// Complete type - should satisfy Layer
struct CompleteLayer {
	void HandleInput() {}
	void Update(float) {}
	void Render() const {}
};

} // namespace

TEST(LayerConceptTest, IncompleteTypesDoNotSatisfyConcept) {
	EXPECT_FALSE(Layer<MissingHandleInput>);
	EXPECT_FALSE(Layer<MissingUpdate>);
	EXPECT_FALSE(Layer<MissingRender>);
	EXPECT_FALSE(Layer<WrongReturnType>);
}

TEST(LayerConceptTest, CompleteTypeSatisfiesConcept) {
	EXPECT_TRUE(Layer<CompleteLayer>);
}
