// Lua Generator Implementation
// Executes Lua scripts to generate procedural assets.

#include "assets/lua/LuaGenerator.h"

#include <utils/Log.h>

#include <filesystem>

namespace engine::assets {

LuaGenerator::LuaGenerator(std::string scriptPath)
	: m_scriptPath(std::move(scriptPath)) {
	// Extract name from script path (filename without extension)
	std::filesystem::path path(m_scriptPath);
	m_name = path.stem().string();
}

bool LuaGenerator::generate(const GenerationContext& ctx, const GeneratorParams& params, GeneratedAsset& out) {
	// Lazy initialization of Lua engine
	if (!m_initialized) {
		if (!m_engine.initialize()) {
			LOG_ERROR(Engine, "Failed to initialize Lua engine for generator: %s", m_name.c_str());
			return false;
		}
		m_initialized = true;
	}

	// Execute the script
	if (!m_engine.executeGenerator(m_scriptPath, ctx, params, out)) {
		LOG_ERROR(Engine, "Lua script execution failed: %s - %s", m_scriptPath.c_str(), m_engine.getLastError().c_str());
		return false;
	}

	return true;
}

void registerLuaGeneratorFactory() {
	// The Lua generator is special - it creates instances based on script path.
	// The AssetRegistry will handle creating LuaGenerator instances directly
	// based on the script path in the asset definition.
	//
	// This function is a placeholder for future integration if we want to
	// register a factory that can dynamically create generators from script names.
	LOG_DEBUG(Engine, "Lua generator support enabled");
}

}  // namespace engine::assets
