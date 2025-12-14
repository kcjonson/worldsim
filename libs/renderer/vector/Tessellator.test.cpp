#include "Tessellator.h"
#include "SVGLoader.h"
#include "Types.h"
#include <cmath>
#include <fstream>
#include <gtest/gtest.h>

using namespace renderer;

namespace {

	// Helper to create a circle polygon with n vertices
	VectorPath createCircle(float cx, float cy, float radius, int numVertices) {
		VectorPath path;
		path.isClosed = true;
		for (int i = 0; i < numVertices; ++i) {
			float angle = 2.0F * M_PI * static_cast<float>(i) / static_cast<float>(numVertices);
			path.vertices.push_back({cx + radius * std::cos(angle), cy + radius * std::sin(angle)});
		}
		return path;
	}

	// Helper to create an ellipse polygon with n vertices
	VectorPath createEllipse(float cx, float cy, float rx, float ry, int numVertices) {
		VectorPath path;
		path.isClosed = true;
		for (int i = 0; i < numVertices; ++i) {
			float angle = 2.0F * M_PI * static_cast<float>(i) / static_cast<float>(numVertices);
			path.vertices.push_back({cx + rx * std::cos(angle), cy + ry * std::sin(angle)});
		}
		return path;
	}

	// Helper to create a simple triangle
	VectorPath createTriangle() {
		VectorPath path;
		path.isClosed = true;
		path.vertices = {{0, 0}, {100, 0}, {50, 100}};
		return path;
	}

	// Helper to create a simple square
	VectorPath createSquare() {
		VectorPath path;
		path.isClosed = true;
		path.vertices = {{0, 0}, {100, 0}, {100, 100}, {0, 100}};
		return path;
	}

} // namespace

class TessellatorTest : public ::testing::Test {
  protected:
	Tessellator tessellator;
};

TEST_F(TessellatorTest, Triangle) {
	auto			path = createTriangle();
	TessellatedMesh mesh;

	bool success = tessellator.Tessellate(path, mesh);
	EXPECT_TRUE(success);
	EXPECT_EQ(mesh.getTriangleCount(), 1);
	EXPECT_EQ(mesh.indices.size(), 3);
}

TEST_F(TessellatorTest, Square) {
	auto			path = createSquare();
	TessellatedMesh mesh;

	bool success = tessellator.Tessellate(path, mesh);
	EXPECT_TRUE(success);
	EXPECT_EQ(mesh.getTriangleCount(), 2);
	EXPECT_EQ(mesh.indices.size(), 6);
}

TEST_F(TessellatorTest, CircleLowResolution) {
	// Simple circle with few vertices - should use fan tessellation
	auto			path = createCircle(50, 50, 30, 8);
	TessellatedMesh mesh;

	bool success = tessellator.Tessellate(path, mesh);
	EXPECT_TRUE(success) << "Circle with 8 vertices failed to tessellate";
	EXPECT_EQ(mesh.getTriangleCount(), 6); // n-2 triangles for fan
}

TEST_F(TessellatorTest, CircleHighResolution) {
	// Circle with many vertices (similar to Bezier-flattened circle from SVG)
	auto			path = createCircle(50, 50, 30, 64);
	TessellatedMesh mesh;

	bool success = tessellator.Tessellate(path, mesh);
	EXPECT_TRUE(success) << "Circle with 64 vertices failed to tessellate";
	EXPECT_EQ(mesh.getTriangleCount(), 62); // n-2 triangles for fan
}

TEST_F(TessellatorTest, EllipseLowResolution) {
	// Ellipse with few vertices
	auto			path = createEllipse(50, 55, 35, 28, 8);
	TessellatedMesh mesh;

	bool success = tessellator.Tessellate(path, mesh);
	EXPECT_TRUE(success) << "Ellipse with 8 vertices failed to tessellate";
	EXPECT_EQ(mesh.getTriangleCount(), 6);
}

TEST_F(TessellatorTest, EllipseHighResolution) {
	// Ellipse with many vertices (similar to Bezier-flattened ellipse from SVG)
	// This is the problematic case according to the bug report
	auto			path = createEllipse(50, 55, 35, 28, 64);
	TessellatedMesh mesh;

	bool success = tessellator.Tessellate(path, mesh);
	EXPECT_TRUE(success) << "Ellipse with 64 vertices failed to tessellate";
	EXPECT_EQ(mesh.getTriangleCount(), 62);
}

TEST_F(TessellatorTest, EllipseVeryHighResolution) {
	// Very high resolution ellipse - tests robustness with many nearly-collinear vertices
	auto			path = createEllipse(50, 55, 35, 28, 256);
	TessellatedMesh mesh;

	bool success = tessellator.Tessellate(path, mesh);
	EXPECT_TRUE(success) << "Ellipse with 256 vertices failed to tessellate";
	EXPECT_EQ(mesh.getTriangleCount(), 254);
}

TEST_F(TessellatorTest, SmallEllipse) {
	// Very small ellipse - tests robustness with small coordinate values
	auto			path = createEllipse(5, 5, 3, 2, 32);
	TessellatedMesh mesh;

	bool success = tessellator.Tessellate(path, mesh);
	EXPECT_TRUE(success) << "Small ellipse failed to tessellate";
	EXPECT_EQ(mesh.getTriangleCount(), 30);
}

TEST_F(TessellatorTest, ThinEllipse) {
	// Very thin ellipse (high aspect ratio) - may trigger edge cases
	auto			path = createEllipse(50, 50, 40, 5, 32);
	TessellatedMesh mesh;

	bool success = tessellator.Tessellate(path, mesh);
	EXPECT_TRUE(success) << "Thin ellipse (40x5) failed to tessellate";
	EXPECT_EQ(mesh.getTriangleCount(), 30);
}

TEST_F(TessellatorTest, BioPileEllipse) {
	// Match the exact ellipse from BioPile SVG: <ellipse cx="50" cy="55" rx="35" ry="28"/>
	// Using 32 vertices to approximate Bezier flattening
	auto			path = createEllipse(50, 55, 35, 28, 32);
	TessellatedMesh mesh;

	bool success = tessellator.Tessellate(path, mesh);
	EXPECT_TRUE(success) << "BioPile-sized ellipse (35x28) failed to tessellate";
	EXPECT_EQ(mesh.getTriangleCount(), 30);
}

// ============================================================================
// SVG Loading Pipeline Tests
// These test the full pipeline: SVG file → nanosvg → Bezier → flatten → tessellate
// ============================================================================

class SVGTessellationTest : public ::testing::Test {
  protected:
	Tessellator tessellator;
	std::string tempDir;

	void SetUp() override { tempDir = "/tmp/svg_test_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()); }

	void TearDown() override {
		// Clean up temp files
		std::remove((tempDir + "_circle.svg").c_str());
		std::remove((tempDir + "_ellipse.svg").c_str());
	}

	// Helper to write SVG content to a temp file
	std::string writeTempSVG(const std::string& suffix, const std::string& content) {
		std::string	  path = tempDir + suffix;
		std::ofstream file(path);
		file << content;
		file.close();
		return path;
	}
};

TEST_F(SVGTessellationTest, CircleFromSVG) {
	// Create an SVG with a simple circle
	std::string svg = R"(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100" width="100" height="100">
  <circle cx="50" cy="50" r="30" fill="#ff0000"/>
</svg>)";

	std::string					path = writeTempSVG("_circle.svg", svg);
	std::vector<LoadedSVGShape> shapes;
	bool						loaded = loadSVG(path, 0.5F, shapes);

	ASSERT_TRUE(loaded) << "Failed to load circle SVG";
	ASSERT_EQ(shapes.size(), 1) << "Expected 1 shape from circle SVG";
	ASSERT_GE(shapes[0].paths.size(), 1) << "Expected at least 1 path in circle shape";

	// Tessellate the circle path
	TessellatedMesh mesh;
	bool			success = tessellator.Tessellate(shapes[0].paths[0], mesh);
	EXPECT_TRUE(success) << "Circle from SVG failed to tessellate. Vertex count: " << shapes[0].paths[0].vertices.size();
	EXPECT_GT(mesh.getTriangleCount(), 0) << "No triangles generated for circle";
}

TEST_F(SVGTessellationTest, EllipseFromSVG) {
	// Create an SVG with an ellipse (this is the problematic case)
	std::string svg = R"(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100" width="100" height="100">
  <ellipse cx="50" cy="55" rx="35" ry="28" fill="#00ff00"/>
</svg>)";

	std::string					path = writeTempSVG("_ellipse.svg", svg);
	std::vector<LoadedSVGShape> shapes;
	bool						loaded = loadSVG(path, 0.5F, shapes);

	ASSERT_TRUE(loaded) << "Failed to load ellipse SVG";
	ASSERT_EQ(shapes.size(), 1) << "Expected 1 shape from ellipse SVG";
	ASSERT_GE(shapes[0].paths.size(), 1) << "Expected at least 1 path in ellipse shape";

	// Tessellate the ellipse path
	TessellatedMesh mesh;
	bool			success = tessellator.Tessellate(shapes[0].paths[0], mesh);
	EXPECT_TRUE(success) << "Ellipse from SVG failed to tessellate. Vertex count: " << shapes[0].paths[0].vertices.size();
	EXPECT_GT(mesh.getTriangleCount(), 0) << "No triangles generated for ellipse";
}

TEST_F(SVGTessellationTest, SmallCircleFromSVG) {
	// Small circles may have fewer flattened vertices
	std::string svg = R"(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100" width="100" height="100">
  <circle cx="50" cy="50" r="3" fill="#0000ff"/>
</svg>)";

	std::string					path = writeTempSVG("_circle.svg", svg);
	std::vector<LoadedSVGShape> shapes;
	bool						loaded = loadSVG(path, 0.5F, shapes);

	ASSERT_TRUE(loaded) << "Failed to load small circle SVG";
	ASSERT_GE(shapes.size(), 1);
	ASSERT_GE(shapes[0].paths.size(), 1);

	TessellatedMesh mesh;
	bool			success = tessellator.Tessellate(shapes[0].paths[0], mesh);
	EXPECT_TRUE(success) << "Small circle from SVG failed to tessellate";
}

TEST_F(SVGTessellationTest, BioPileSVGEllipses) {
	// Test the exact BioPile SVG structure with multiple ellipses
	std::string svg = R"(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100" width="100" height="100">
  <ellipse cx="50" cy="55" rx="35" ry="28" fill="#5a4a30" opacity="0.4"/>
  <ellipse cx="55" cy="50" rx="18" ry="15" fill="#5a4a30"/>
  <ellipse cx="48" cy="45" rx="10" ry="8" fill="#6a5a40"/>
  <ellipse cx="58" cy="55" rx="8" ry="6" fill="#6a5a40"/>
</svg>)";

	std::string					path = writeTempSVG("_ellipse.svg", svg);
	std::vector<LoadedSVGShape> shapes;
	bool						loaded = loadSVG(path, 0.5F, shapes);

	ASSERT_TRUE(loaded) << "Failed to load BioPile-style SVG";
	EXPECT_EQ(shapes.size(), 4) << "Expected 4 ellipse shapes";

	// Tessellate all ellipse paths
	for (size_t i = 0; i < shapes.size(); ++i) {
		ASSERT_GE(shapes[i].paths.size(), 1) << "Shape " << i << " has no paths";
		TessellatedMesh mesh;
		bool			success = tessellator.Tessellate(shapes[i].paths[0], mesh);
		EXPECT_TRUE(success) << "BioPile ellipse " << i << " failed to tessellate. Vertices: " << shapes[i].paths[0].vertices.size();
	}
}

TEST_F(SVGTessellationTest, BerryBushCircles) {
	// Test circle elements like in berry_bush.svg
	std::string svg = R"(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100" width="100" height="100">
  <circle cx="35" cy="30" r="4" fill="#d62828"/>
  <circle cx="42" cy="33" r="3" fill="#c41e1e"/>
  <circle cx="38" cy="38" r="3.5" fill="#d62828"/>
  <circle cx="34" cy="29" r="1" fill="#ffffff" opacity="0.6"/>
</svg>)";

	std::string					path = writeTempSVG("_circle.svg", svg);
	std::vector<LoadedSVGShape> shapes;
	bool						loaded = loadSVG(path, 0.5F, shapes);

	ASSERT_TRUE(loaded) << "Failed to load berry circles SVG";
	EXPECT_EQ(shapes.size(), 4) << "Expected 4 circle shapes";

	for (size_t i = 0; i < shapes.size(); ++i) {
		ASSERT_GE(shapes[i].paths.size(), 1) << "Shape " << i << " has no paths";
		TessellatedMesh mesh;
		bool			success = tessellator.Tessellate(shapes[i].paths[0], mesh);
		EXPECT_TRUE(success) << "Berry circle " << i << " failed to tessellate. Vertices: " << shapes[i].paths[0].vertices.size();
	}
}
