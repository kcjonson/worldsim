// Grass Blade Generator Implementation
// Generates grass blade shapes using Bezier curves.
// Based on the algorithm from GrassScene demo.

#include "assets/generators/GrassBladeGenerator.h"

#include <vector/Bezier.h>

namespace engine::assets {

void registerGrassBladeGenerator() {
	GeneratorRegistry::Get().registerGenerator("GrassBlade", []() -> std::unique_ptr<IAssetGenerator> {
		return std::make_unique<GrassBladeGenerator>();
	});
}

bool GrassBladeGenerator::generate(const GenerationContext& ctx, const GeneratorParams& params, GeneratedAsset& out) {
	using namespace Foundation;

	out.clear();

	// Get parameters with defaults matching the original GrassScene
	float heightMin = 10.0F;
	float heightMax = 14.0F;
	params.getFloatRange("heightRange", heightMin, heightMax, 10.0F, 14.0F);

	float widthMin = 1.6F;
	float widthMax = 2.4F;
	params.getFloatRange("widthRange", widthMin, widthMax, 1.6F, 2.4F);

	float bendMin = -0.3F;
	float bendMax = 0.3F;
	params.getFloatRange("bendRange", bendMin, bendMax, -0.3F, 0.3F);

	float curveTolerance = params.getFloat("curveTolerance", 1.0F);

	// Use context for deterministic randomness
	// For the template (single blade), we generate a "default" blade
	// Per-instance variation will come from GPU instancing
	std::mt19937						 rng = ctx.createRng();
	std::uniform_real_distribution<float> heightDist(heightMin, heightMax);
	std::uniform_real_distribution<float> widthDist(widthMin, widthMax);
	std::uniform_real_distribution<float> bendDist(bendMin, bendMax);

	// Generate blade dimensions
	// For the template, use middle values (not randomized)
	// This gives a clean template that instances can scale/rotate
	float bladeHeight = (heightMin + heightMax) / 2.0F;
	float bladeBaseWidth = (widthMin + widthMax) / 2.0F;
	float bendAmount = 0.0F;  // Template has no bend; wind adds bend at runtime

	// Calculate derived values - using proportional control points
	float bladeTipX = bladeBaseWidth / 2.0F;
	float bendOffset = bendAmount * bladeHeight;  // Proportional bend

	// Control point curvature proportional to blade width
	float controlCurve = bladeBaseWidth * 0.8F;

	// Create blade path using Bezier curves
	GeneratedPath bladePath;
	bladePath.fillColor = Color(0.3F, 0.6F, 0.2F, 1.0F);  // Default grass green
	bladePath.isClosed = true;

	// Left edge curve: bottom-left to tip
	renderer::CubicBezier leftEdge = {
		.p0 = {0.0F, 0.0F},
		.p1 = {-controlCurve + bendOffset * 0.3F, -bladeHeight * 0.33F},
		.p2 = {bladeTipX - controlCurve + bendOffset * 0.7F, -bladeHeight * 0.83F},
		.p3 = {bladeTipX + bendOffset, -bladeHeight}
	};

	// Right edge curve: tip to bottom-right
	renderer::CubicBezier rightEdge = {
		.p0 = {bladeTipX + bendOffset, -bladeHeight},
		.p1 = {bladeTipX + controlCurve + bendOffset * 0.7F, -bladeHeight * 0.83F},
		.p2 = {bladeBaseWidth + controlCurve + bendOffset * 0.3F, -bladeHeight * 0.33F},
		.p3 = {bladeBaseWidth, 0.0F}
	};

	// Build vertex list
	bladePath.vertices.push_back(leftEdge.p0);  // Start point

	// Flatten left edge
	renderer::flattenCubicBezier(leftEdge, curveTolerance, bladePath.vertices);

	// Flatten right edge (continues from tip)
	renderer::flattenCubicBezier(rightEdge, curveTolerance, bladePath.vertices);

	// Close the path back to origin (implied by isClosed)

	out.addPath(std::move(bladePath));

	return true;
}

}  // namespace engine::assets
