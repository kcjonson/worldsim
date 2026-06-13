#include "primitives/BatchRenderer.h"
#include <gtest/gtest.h>

using namespace Renderer;

// ============================================================================
// UberVertex Structure Tests (no GL context required)
// ============================================================================

TEST(UberVertexTest, SizeIsReasonable) {
	// position (8) + texCoord (8) + color (16) + data1 (16) + data2 (16) + clipBounds (16) = 80
	// However, Foundation types may vary - verify it's reasonable
	static_assert(sizeof(UberVertex) <= 96, "UberVertex should be <= 96 bytes");
}

// ============================================================================
// BatchRenderer Tests
// Note: Tests requiring OpenGL context are limited in scope
// ============================================================================

TEST(BatchRendererTest, RenderModeConstants) {
	// Verify render mode constant matches expected value
	EXPECT_EQ(kRenderModeText, -1.0F);
}
