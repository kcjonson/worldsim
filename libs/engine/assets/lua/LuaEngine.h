#pragma once

// Lua Scripting Engine
// Manages Lua state, API bindings, and script execution for procedural asset generation.
// Uses sol2 for C++ <-> Lua bindings.

#include "assets/IAssetGenerator.h"

#include <memory>
#include <string>

// Forward declare sol types to avoid header pollution
namespace sol {
class state;
}

namespace engine::assets {

/// Lua scripting engine for procedural asset generation.
/// Provides a sandboxed Lua environment with access to asset generation APIs.
class LuaEngine {
  public:
	LuaEngine();
	~LuaEngine();

	// Non-copyable
	LuaEngine(const LuaEngine&) = delete;
	LuaEngine& operator=(const LuaEngine&) = delete;

	// Movable
	LuaEngine(LuaEngine&&) noexcept;
	LuaEngine& operator=(LuaEngine&&) noexcept;

	/// Initialize the Lua state and register API bindings
	/// @return true if initialization succeeded
	bool initialize();

	/// Execute a generator script file
	/// @param scriptPath Path to the Lua script
	/// @param ctx Generation context (seed, variant)
	/// @param params Parameters from asset definition
	/// @param outAsset Output generated asset
	/// @return true if script executed successfully
	bool executeGenerator(
		const std::string& scriptPath,
		const GenerationContext& ctx,
		const GeneratorParams& params,
		GeneratedAsset& outAsset
	);

	/// Check if the engine is initialized
	bool isInitialized() const { return m_initialized; }

	/// Get the last error message
	const std::string& getLastError() const { return m_lastError; }

  private:
	/// Register all API bindings (Vec2, Color, Path, Asset)
	void registerBindings();

	/// Set up a sandboxed environment for script execution
	void setupSandbox();

	std::unique_ptr<sol::state> m_lua;
	bool m_initialized = false;
	std::string m_lastError;
};

}  // namespace engine::assets
