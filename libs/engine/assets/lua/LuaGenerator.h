#pragma once

// Lua Generator
// IAssetGenerator implementation that executes Lua scripts for procedural generation.

#include "assets/IAssetGenerator.h"
#include "assets/lua/LuaEngine.h"

#include <memory>
#include <string>

namespace engine::assets {

/// Generator that executes Lua scripts for procedural asset generation.
/// Each LuaGenerator instance is associated with a specific script path.
class LuaGenerator : public IAssetGenerator {
  public:
	/// Create a Lua generator for a specific script
	/// @param scriptPath Path to the Lua script file
	explicit LuaGenerator(std::string scriptPath);
	~LuaGenerator() override = default;

	// Non-copyable (owns LuaEngine)
	LuaGenerator(const LuaGenerator&) = delete;
	LuaGenerator& operator=(const LuaGenerator&) = delete;

	// Movable
	LuaGenerator(LuaGenerator&&) noexcept = default;
	LuaGenerator& operator=(LuaGenerator&&) noexcept = default;

	/// Generate an asset by executing the Lua script
	bool generate(const GenerationContext& ctx, const GeneratorParams& params, GeneratedAsset& out) override;

	/// Lua scripts can produce complex assets
	AssetComplexity getComplexity() const override { return AssetComplexity::Complex; }

	/// Lua assets don't have built-in animation (yet)
	AnimationType getAnimationType() const override { return AnimationType::None; }

	/// Get the generator name (script filename without extension)
	const char* getName() const override { return m_name.c_str(); }

	/// Get the full script path
	const std::string& getScriptPath() const { return m_scriptPath; }

  private:
	std::string m_scriptPath;
	std::string m_name;
	LuaEngine m_engine;
	bool m_initialized = false;
};

/// Factory function type for creating Lua generators
/// This registers the "Lua" generator with the GeneratorRegistry
void registerLuaGeneratorFactory();

}  // namespace engine::assets
