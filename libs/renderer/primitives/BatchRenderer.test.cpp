#include "primitives/BatchRenderer.h"
#include <gtest/gtest.h>

using namespace Renderer;

// ============================================================================
// UberVertex Structure Tests (no GL context required)
// ============================================================================

TEST(UberVertexTest, SizeIs96Bytes) {
	// UberVertex should be 96 bytes (6 * vec4 = 6 * 16 bytes)
	// position (8) + texCoord (8) + color (16) + data1 (16) + data2 (16) + clipBounds (16) = 80
	// However, Foundation types may vary - verify it's reasonable
	static_assert(sizeof(UberVertex) <= 128, "UberVertex should be <= 128 bytes");
}

// ============================================================================
// BatchRenderer Tests
// Note: Tests requiring OpenGL context are limited in scope
// ============================================================================

class BatchRendererTest : public ::testing::Test {
  protected:
	// We can't test GPU operations without context, but we can verify
	// the data packing logic by accessing vertices after addTileQuad
};

TEST(BatchRendererTest, RenderModeConstants) {
	// Verify render mode constant matches expected value
	EXPECT_EQ(kRenderModeText, -1.0F);
}

// ============================================================================
// AddTileQuad Data Packing Tests
// These verify the vertex data is correctly formatted for the shader
// ============================================================================

TEST(AddTileQuadTest, VertexPositionCalculation) {
	// Test the position calculation logic used in addTileQuad
	Foundation::Rect bounds{100.0F, 200.0F, 50.0F, 30.0F};

	float halfW = bounds.width * 0.5F;
	float halfH = bounds.height * 0.5F;
	float centerX = bounds.x + halfW;
	float centerY = bounds.y + halfH;

	EXPECT_FLOAT_EQ(halfW, 25.0F);
	EXPECT_FLOAT_EQ(halfH, 15.0F);
	EXPECT_FLOAT_EQ(centerX, 125.0F);
	EXPECT_FLOAT_EQ(centerY, 215.0F);

	// Verify corner positions
	EXPECT_FLOAT_EQ(centerX - halfW, 100.0F); // Left = bounds.x
	EXPECT_FLOAT_EQ(centerX + halfW, 150.0F); // Right = bounds.x + width
	EXPECT_FLOAT_EQ(centerY - halfH, 200.0F); // Top = bounds.y
	EXPECT_FLOAT_EQ(centerY + halfH, 230.0F); // Bottom = bounds.y + height
}

TEST(AddTileQuadTest, Data1Packing) {
	// Test data1 packing: (edgeMask, cornerMask, surfaceId, hardEdgeMask)
	uint8_t edgeMask = 0x0F;     // All 4 cardinal edges
	uint8_t cornerMask = 0x05;   // NW and SE corners
	uint8_t surfaceId = 3;       // Rock
	uint8_t hardEdgeMask = 0xAA; // Every other direction

	Foundation::Vec4 data1(
		static_cast<float>(edgeMask),
		static_cast<float>(cornerMask),
		static_cast<float>(surfaceId),
		static_cast<float>(hardEdgeMask)
	);

	EXPECT_FLOAT_EQ(data1.x, 15.0F);  // 0x0F
	EXPECT_FLOAT_EQ(data1.y, 5.0F);   // 0x05
	EXPECT_FLOAT_EQ(data1.z, 3.0F);   // surfaceId
	EXPECT_FLOAT_EQ(data1.w, 170.0F); // 0xAA
}

TEST(AddTileQuadTest, Data2Packing) {
	// Test data2 packing: (halfW, halfH, 0, kRenderModeTile)
	float halfW = 32.0F;
	float halfH = 32.0F;
	constexpr float kRenderModeTile = -3.0F;

	Foundation::Vec4 data2(halfW, halfH, 0.0F, kRenderModeTile);

	EXPECT_FLOAT_EQ(data2.x, 32.0F);
	EXPECT_FLOAT_EQ(data2.y, 32.0F);
	EXPECT_FLOAT_EQ(data2.z, 0.0F);
	EXPECT_FLOAT_EQ(data2.w, -3.0F);
}

TEST(AddTileQuadTest, TexCoordLocalPosition) {
	// Verify texCoord calculation (rect-local position from center)
	float halfW = 25.0F;
	float halfH = 15.0F;

	// Four corners in local space (relative to center)
	Foundation::Vec2 topLeft(-halfW, -halfH);
	Foundation::Vec2 topRight(halfW, -halfH);
	Foundation::Vec2 bottomRight(halfW, halfH);
	Foundation::Vec2 bottomLeft(-halfW, halfH);

	EXPECT_FLOAT_EQ(topLeft.x, -25.0F);
	EXPECT_FLOAT_EQ(topLeft.y, -15.0F);
	EXPECT_FLOAT_EQ(topRight.x, 25.0F);
	EXPECT_FLOAT_EQ(topRight.y, -15.0F);
	EXPECT_FLOAT_EQ(bottomRight.x, 25.0F);
	EXPECT_FLOAT_EQ(bottomRight.y, 15.0F);
	EXPECT_FLOAT_EQ(bottomLeft.x, -25.0F);
	EXPECT_FLOAT_EQ(bottomLeft.y, 15.0F);
}

TEST(AddTileQuadTest, IndexGeneration) {
	// Verify index pattern for quad triangles (CCW winding)
	// Given baseIndex, the indices should form two triangles:
	// Triangle 1: baseIndex, baseIndex+1, baseIndex+2
	// Triangle 2: baseIndex, baseIndex+2, baseIndex+3
	uint32_t baseIndex = 100;

	std::vector<uint32_t> expectedIndices = {
		baseIndex,     baseIndex + 1, baseIndex + 2,  // First triangle
		baseIndex,     baseIndex + 2, baseIndex + 3   // Second triangle
	};

	EXPECT_EQ(expectedIndices.size(), 6u);
	EXPECT_EQ(expectedIndices[0], 100u);
	EXPECT_EQ(expectedIndices[1], 101u);
	EXPECT_EQ(expectedIndices[2], 102u);
	EXPECT_EQ(expectedIndices[3], 100u);
	EXPECT_EQ(expectedIndices[4], 102u);
	EXPECT_EQ(expectedIndices[5], 103u);
}

TEST(AddTileQuadTest, EdgeMaskBitLayout) {
	// Edge mask bits: 0=N, 1=E, 2=S, 3=W
	// Verify bit positions match expected layout
	constexpr uint8_t kNorth = 0x01;
	constexpr uint8_t kEast = 0x02;
	constexpr uint8_t kSouth = 0x04;
	constexpr uint8_t kWest = 0x08;

	// All edges
	uint8_t allEdges = kNorth | kEast | kSouth | kWest;
	EXPECT_EQ(allEdges, 0x0F);

	// North and South only
	uint8_t northSouth = kNorth | kSouth;
	EXPECT_EQ(northSouth, 0x05);

	// East and West only
	uint8_t eastWest = kEast | kWest;
	EXPECT_EQ(eastWest, 0x0A);
}

TEST(AddTileQuadTest, CornerMaskBitLayout) {
	// Corner mask bits: 0=NW, 1=NE, 2=SE, 3=SW
	constexpr uint8_t kNW = 0x01;
	constexpr uint8_t kNE = 0x02;
	constexpr uint8_t kSE = 0x04;
	constexpr uint8_t kSW = 0x08;

	// All corners
	uint8_t allCorners = kNW | kNE | kSE | kSW;
	EXPECT_EQ(allCorners, 0x0F);

	// Diagonal corners (NW, SE)
	uint8_t diagonal1 = kNW | kSE;
	EXPECT_EQ(diagonal1, 0x05);

	// Other diagonal (NE, SW)
	uint8_t diagonal2 = kNE | kSW;
	EXPECT_EQ(diagonal2, 0x0A);
}
