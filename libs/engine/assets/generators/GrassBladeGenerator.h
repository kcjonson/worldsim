#pragma once

// Grass Blade Generator
// Procedural generator for grass blade assets.
// Extracted from GrassScene demo for use with the Asset System.

#include "assets/IAssetGenerator.h"

namespace engine::assets {

/// Register the GrassBlade generator with the GeneratorRegistry.
/// Call this before using AssetRegistry::generateAsset with GrassBlade.
void registerGrassBladeGenerator();

/// Generates a single grass blade shape using Bezier curves.
/// The blade is defined by:
/// - Height and width ranges
/// - Bend amount (for wind animation)
/// - Curve tolerance for tessellation quality
///
/// Output is a closed polygon path ready for tessellation.
class GrassBladeGenerator : public IAssetGenerator {
  public:
	GrassBladeGenerator() = default;

	/// Generate a grass blade shape
	bool generate(const GenerationContext& ctx, const GeneratorParams& params, GeneratedAsset& out) override;

	/// Grass is a simple asset (uses GPU instancing)
	AssetComplexity getComplexity() const override { return AssetComplexity::Simple; }

	/// Grass uses parametric wind animation in vertex shader
	AnimationType getAnimationType() const override { return AnimationType::Parametric; }

	/// Generator name for registration
	const char* getName() const override { return "GrassBlade"; }
};

}  // namespace engine::assets
