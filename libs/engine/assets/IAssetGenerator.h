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
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::assets {

/// Context provided to generators during asset generation
struct GenerationContext {
	uint32_t seed = 0;			// Deterministic seed for RNG
	uint32_t variantIndex = 0;	// Which variant is being generated

	/// Create a seeded random number generator
	std::mt19937 createRng() const { return std::mt19937(seed + variantIndex); }

	/// Generate a random float in range [min, max]
	float randomFloat(float min, float max) const {
		std::mt19937						 rng(seed + variantIndex);
		std::uniform_real_distribution<float> dist(min, max);
		return dist(rng);
	}

	/// Generate a random float in range [min, max] with additional offset
	float randomFloat(float min, float max, uint32_t offset) const {
		std::mt19937						 rng(seed + variantIndex + offset);
		std::uniform_real_distribution<float> dist(min, max);
		return dist(rng);
	}
};

/// A single path (filled polygon) in a generated asset
struct GeneratedPath {
	std::vector<Foundation::Vec2> vertices;
	Foundation::Color			  fillColor{0.3F, 0.6F, 0.2F, 1.0F};	 // Default grass green
	bool						  isClosed = true;

	void clear() {
		vertices.clear();
		fillColor = Foundation::Color(0.3F, 0.6F, 0.2F, 1.0F);
		isClosed = true;
	}
};

/// Output structure for generated assets
struct GeneratedAsset {
	std::vector<GeneratedPath> paths;

	void clear() { paths.clear(); }

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
		engine::assets::GeneratorRegistry::Get().registerGenerator(                                                  \
			name, []() -> std::unique_ptr<engine::assets::IAssetGenerator> {                                         \
				return std::make_unique<FullClass>();                                                                \
			});                                                                                                      \
		return true;                                                                                                 \
	}()

}  // namespace engine::assets
