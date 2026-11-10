#pragma once

// Lua Scripting Engine
// Manages Lua state, API bindings, and script execution for procedural asset generation.
// Uses sol2 for C++ <-> Lua bindings.

#include "assets/IAssetGenerator.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

// Forward declare sol types to avoid header pollution
namespace sol {
class state;
}

namespace assets {

/// The prelude files found in a folder, or why they could not be listed (error is empty on success).
struct PreludeFiles {
	std::vector<std::filesystem::path> files;
	std::string						   error;
};

/// Lua scripting engine for procedural asset generation.
/// Provides a sandboxed Lua environment with access to asset generation APIs.
class LuaEngine {
  public:
	/// Part of the ground bake cache key: bump it when a binding or builtin changes what a script generates.
	static constexpr uint32_t kApiVersion = 1;

	LuaEngine();
	~LuaEngine();

	// Non-copyable
	LuaEngine(const LuaEngine&) = delete;
	LuaEngine& operator=(const LuaEngine&) = delete;

	// Movable
	LuaEngine(LuaEngine&&) noexcept;
	LuaEngine& operator=(LuaEngine&&) noexcept;

	/// The prelude: every .lua file directly in dir, sorted by file name. No files, and no error, when dir is
	/// empty or missing. A dir that is not a folder, or any filesystem error while listing it, sets error
	/// (naming the path) and leaves files empty. The ground bake cache keys these files, so it reads the
	/// same list the engine runs.
	[[nodiscard]] static PreludeFiles preludeFiles(const std::filesystem::path& dir);

	/// Initialize the Lua state and register API bindings
	/// @param preludeDir Folder of prelude scripts that run before each generator (see preludeFiles)
	/// @return true if initialization succeeded
	bool initialize(std::filesystem::path preludeDir = {});

	/// Execute a generator script file, after the prelude. A prelude file that cannot be read or
	/// fails to run fails the call, with the file named in getLastError.
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

	/// Run the prelude files in the state; false (m_lastError set, logged) at the first that fails
	bool runPrelude();

	std::unique_ptr<sol::state> m_lua;
	std::filesystem::path m_preludeDir;
	bool m_initialized = false;
	std::string m_lastError;
};

}  // namespace assets
