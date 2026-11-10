#pragma once

// Asset Generator Interface
// Defines the interface for procedural asset generators.
// Designed for C++ now with Lua drop-in compatibility later.

#include "assets/AssetDefinition.h"

#include <graphics/Color.h>
#include <math/Types.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace assets {

/// Context provided to generators during asset generation
struct GenerationContext {
	uint32_t seed = 0;			// Deterministic seed for RNG; already distinct per variant
	uint32_t variantIndex = 0;	// Which variant of its age class is being generated
	std::optional<std::string> ageClass;	// The age class's name; unset for a def without <ageClasses>
	std::optional<float> periodMeters;		// A ground def's <period>, the tile a variant covers; unset for other roles
};

/// A single path (filled polygon) in a generated asset
struct GeneratedPath {
	std::vector<Foundation::Vec2> vertices;
	Foundation::Color			  fillColor{0.3F, 0.6F, 0.2F, 1.0F};	 // Default grass green
	bool						  isClosed = true;
	// Drawn only where the screen shows at least this many pixels per meter (path:setLod in Lua).
	// 0 = always drawn. The ground tile bake reads it to leave fine detail out of coarse mips.
	float						  minPixelsPerMeter = 0.0F;
	// The ground tile bake's height for this path, 0 to 1 (path:setHeight in Lua). Unset, a ground
	// path takes its paint-order rank.
	std::optional<float>		  height;

	void clear() {
		vertices.clear();
		fillColor = Foundation::Color(0.3F, 0.6F, 0.2F, 1.0F);
		isClosed = true;
		minPixelsPerMeter = 0.0F;
		height.reset();
	}
};

/// Output structure for generated assets
struct GeneratedAsset {
	std::vector<GeneratedPath> paths;

	// Collision footprint emitted by the generator script (asset:setCollisionRect).
	// Captured eagerly at load so nav/collision see it without running the full
	// render pipeline. Unset means the generator declared no collision.
	std::optional<CollisionShape> emittedCollision;

	void clear() {
		paths.clear();
		emittedCollision.reset();
	}

	/// Add a path to the asset
	void addPath(GeneratedPath&& path) { paths.push_back(std::move(path)); }

	/// Add a path to the asset (copy)
	void addPath(const GeneratedPath& path) { paths.push_back(path); }
};

/// Interface for procedural asset generators.
/// Generators are stateless and produce assets from parameters and a seeded context.
class IAssetGenerator {
  public:
	virtual ~IAssetGenerator() = default;

	/// Generate an asset with the given context and parameters.
	/// @param ctx Generation context (seed, variant index)
	/// @param params Parameters from asset definition XML
	/// @param out Output asset (cleared before use)
	/// @return true if generation succeeded
	virtual bool generate(const GenerationContext& ctx, const GeneratorParams& params, GeneratedAsset& out) = 0;

	/// Get the complexity hint for this generator's output
	virtual AssetComplexity getComplexity() const = 0;

	/// Get the animation type this generator's assets support
	virtual AnimationType getAnimationType() const = 0;

	/// Get the generator's name (for registration)
	virtual const char* getName() const = 0;
};

/// Factory function type for creating generators
using GeneratorFactory = std::function<std::unique_ptr<IAssetGenerator>()>;

/// Registry for asset generators.
/// Generators register themselves at static initialization time.
class GeneratorRegistry {
  public:
	/// Get the singleton registry instance
	static GeneratorRegistry& Get();

	/// Register a generator factory
	void registerGenerator(const char* name, GeneratorFactory factory);

	/// Create a generator instance by name
	std::unique_ptr<IAssetGenerator> create(const char* name);

	/// Check if a generator exists
	bool hasGenerator(const char* name) const;

  private:
	GeneratorRegistry() = default;
	std::unordered_map<std::string, GeneratorFactory> factories;
};

/// Macro for registering generators at static initialization time.
/// Usage inside namespace: REGISTER_GENERATOR("Name", ClassName)
/// Usage outside namespace: REGISTER_GENERATOR_FULL("Name", shortname, fully::qualified::ClassName)
#define REGISTER_GENERATOR(name, Class)                                                                              \
	static bool g_##Class##_registered = []() {                                                                      \
		GeneratorRegistry::Get().registerGenerator(name, []() -> std::unique_ptr<IAssetGenerator> {                  \
			return std::make_unique<Class>();                                                                        \
		});                                                                                                          \
		return true;                                                                                                 \
	}()

/// Macro for registering generators outside their namespace (requires short name for variable naming)
#define REGISTER_GENERATOR_FULL(name, ShortName, FullClass)                                                          \
	static bool g_##ShortName##_registered = []() {                                                                  \
		assets::GeneratorRegistry::Get().registerGenerator(                                                  \
			name, []() -> std::unique_ptr<assets::IAssetGenerator> {                                         \
				return std::make_unique<FullClass>();                                                                \
			});                                                                                                      \
		return true;                                                                                                 \
	}()

}  // namespace assets
