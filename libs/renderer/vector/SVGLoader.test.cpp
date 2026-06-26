#include "vector/SVGLoader.h"

#include "vector/Types.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace {

// Write `content` to a unique temp .svg, return its path. Caller removes it.
std::filesystem::path writeTempSvg(const std::string& name, const std::string& content) {
	const std::filesystem::path p = std::filesystem::temp_directory_path() / name;
	std::ofstream				out(p);
	out << content;
	out.close();
	return p;
}

renderer::LoadedSVGShape strokeShape(const std::vector<Foundation::Vec2>& verts, bool closed, float width) {
	renderer::LoadedSVGShape s;
	s.hasFill = false;
	s.hasStroke = true;
	s.strokeWidth = width;
	s.strokeColor = Foundation::Color::white();
	s.paths.emplace_back(verts, closed);
	return s;
}

} // namespace

// A stroked straight segment flattens to 2 vertices; loadSVG must keep it (it used to drop any
// path with < 3 vertices, which silently discarded stroke-only lines and their whole shape).
TEST(SVGLoaderTest, LoadStrokeOnlyLineKeepsTwoVertexPath) {
	const std::filesystem::path svg = writeTempSvg(
		"worldsim_stroke_line.svg",
		R"(<svg xmlns="http://www.w3.org/2000/svg" width="20" height="20" viewBox="0 0 20 20">
  <path d="M0,10 L20,10" stroke="#ff0000" stroke-width="2" fill="none"/>
</svg>)");

	std::vector<renderer::LoadedSVGShape> shapes;
	const bool							  ok = renderer::loadSVG(svg.string(), 0.5F, shapes);
	std::filesystem::remove(svg);

	ASSERT_TRUE(ok);
	ASSERT_EQ(shapes.size(), 1U);
	EXPECT_FALSE(shapes[0].hasFill);
	EXPECT_TRUE(shapes[0].hasStroke);
	ASSERT_EQ(shapes[0].paths.size(), 1U);
	EXPECT_GE(shapes[0].paths[0].vertices.size(), 2U);
}

// An open 2-point stroke is exactly one bevel-band quad: 4 verts, 2 triangles, no join wedges.
TEST(SVGLoaderTest, StrokeOnlyOpenSegmentEmitsOneQuad) {
	const auto				 shape = strokeShape({{0.0F, 0.0F}, {10.0F, 0.0F}}, /*closed=*/false, 2.0F);
	renderer::TessellatedMesh mesh;
	renderer::appendShapeMesh(shape, mesh);

	EXPECT_EQ(mesh.vertices.size(), 4U);
	EXPECT_EQ(mesh.indices.size(), 6U);
	EXPECT_EQ(mesh.getTriangleCount(), 2U);
	EXPECT_EQ(mesh.colors.size(), mesh.vertices.size()); // per-vertex colors stay parallel
}

// Closing the polyline adds join wedges at every corner, so it has more geometry than the open one.
TEST(SVGLoaderTest, ClosedStrokeAddsJoinsVsOpen) {
	const std::vector<Foundation::Vec2> corner = {{0.0F, 0.0F}, {10.0F, 0.0F}, {10.0F, 10.0F}};

	renderer::TessellatedMesh openMesh;
	renderer::appendShapeMesh(strokeShape(corner, /*closed=*/false, 2.0F), openMesh);

	renderer::TessellatedMesh closedMesh;
	renderer::appendShapeMesh(strokeShape(corner, /*closed=*/true, 2.0F), closedMesh);

	EXPECT_GT(openMesh.vertices.size(), 8U);						// 2 quads + at least one join wedge
	EXPECT_GT(closedMesh.vertices.size(), openMesh.vertices.size()); // extra segment + joins all the way round
}

// hasFill still drives the fill pass; a filled triangle emits at least one tessellated triangle.
TEST(SVGLoaderTest, FillEmittedWhenHasFill) {
	renderer::LoadedSVGShape shape;
	shape.hasFill = true;
	shape.fillColor = Foundation::Color::white();
	shape.paths.emplace_back(std::vector<Foundation::Vec2>{{0.0F, 0.0F}, {10.0F, 0.0F}, {5.0F, 10.0F}}, true);

	renderer::TessellatedMesh mesh;
	renderer::appendShapeMesh(shape, mesh);

	EXPECT_GE(mesh.vertices.size(), 3U);
	EXPECT_GE(mesh.getTriangleCount(), 1U);
	EXPECT_EQ(mesh.colors.size(), mesh.vertices.size());
}
