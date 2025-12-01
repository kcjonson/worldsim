// Lua Engine Implementation
// Initializes sol2 Lua state and registers asset generation API bindings.

#include "assets/lua/LuaEngine.h"

#include <graphics/Color.h>
#include <math/Types.h>
#include <utils/Log.h>

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include <fstream>
#include <sstream>

namespace engine::assets {

LuaEngine::LuaEngine() = default;
LuaEngine::~LuaEngine() = default;

LuaEngine::LuaEngine(LuaEngine&&) noexcept = default;
LuaEngine& LuaEngine::operator=(LuaEngine&&) noexcept = default;

bool LuaEngine::initialize() {
	if (m_initialized) {
		return true;
	}

	try {
		m_lua = std::make_unique<sol::state>();
		m_lua->open_libraries(
			sol::lib::base,
			sol::lib::math,
			sol::lib::string,
			sol::lib::table
		);

		registerBindings();
		setupSandbox();

		m_initialized = true;
		LOG_INFO(Engine, "Lua scripting engine initialized");
		return true;

	} catch (const sol::error& e) {
		m_lastError = std::string("Lua initialization failed: ") + e.what();
		LOG_ERROR(Engine, "%s", m_lastError.c_str());
		return false;
	}
}

void LuaEngine::registerBindings() {
	using namespace Foundation;

	// Register Vec2 type
	m_lua->new_usertype<Vec2>("Vec2",
		sol::constructors<Vec2(), Vec2(float, float)>(),
		"x", &Vec2::x,
		"y", &Vec2::y,
		// Arithmetic operations
		sol::meta_function::addition, [](const Vec2& a, const Vec2& b) { return Vec2{a.x + b.x, a.y + b.y}; },
		sol::meta_function::subtraction, [](const Vec2& a, const Vec2& b) { return Vec2{a.x - b.x, a.y - b.y}; },
		sol::meta_function::multiplication, sol::overload(
			[](const Vec2& v, float s) { return Vec2{v.x * s, v.y * s}; },
			[](float s, const Vec2& v) { return Vec2{v.x * s, v.y * s}; }
		),
		// Utility functions
		"length", [](const Vec2& v) { return std::sqrt(v.x * v.x + v.y * v.y); },
		"normalize", [](const Vec2& v) {
			float len = std::sqrt(v.x * v.x + v.y * v.y);
			if (len > 0.0001F) {
				return Vec2{v.x / len, v.y / len};
			}
			return v;
		},
		"dot", [](const Vec2& a, const Vec2& b) { return a.x * b.x + a.y * b.y; }
	);

	// Register Color type
	m_lua->new_usertype<Color>("Color",
		sol::constructors<Color(), Color(float, float, float, float)>(),
		"r", &Color::r,
		"g", &Color::g,
		"b", &Color::b,
		"a", &Color::a,
		// Factory functions
		"rgb", [](float r, float g, float b) { return Color(r, g, b, 1.0F); },
		"rgba", [](float r, float g, float b, float a) { return Color(r, g, b, a); },
		// Utility
		"lerp", [](const Color& a, const Color& b, float t) {
			return Color(
				a.r + (b.r - a.r) * t,
				a.g + (b.g - a.g) * t,
				a.b + (b.b - a.b) * t,
				a.a + (b.a - a.a) * t
			);
		}
	);

	// Register GeneratedPath type
	m_lua->new_usertype<GeneratedPath>("Path",
		sol::constructors<GeneratedPath()>(),
		"vertices", &GeneratedPath::vertices,
		"fillColor", &GeneratedPath::fillColor,
		"isClosed", &GeneratedPath::isClosed,
		// Convenience methods
		"addVertex", [](GeneratedPath& path, float x, float y) {
			path.vertices.push_back(Vec2{x, y});
		},
		"setColor", [](GeneratedPath& path, float r, float g, float b, float a) {
			path.fillColor = Color(r, g, b, a);
		},
		"close", [](GeneratedPath& path) {
			path.isClosed = true;
		},
		"clear", &GeneratedPath::clear
	);

	// Register GeneratedAsset type
	m_lua->new_usertype<GeneratedAsset>("Asset",
		sol::constructors<GeneratedAsset()>(),
		"paths", &GeneratedAsset::paths,
		// Convenience methods
		"addPath", [](GeneratedAsset& asset, const GeneratedPath& path) {
			asset.addPath(path);
		},
		"clear", &GeneratedAsset::clear,
		"createPath", [](GeneratedAsset& asset) -> GeneratedPath& {
			asset.paths.emplace_back();
			return asset.paths.back();
		}
	);

	// Register utility math functions
	auto& lua = *m_lua;
	lua["lerp"] = [](float a, float b, float t) { return a + (b - a) * t; };
	lua["clamp"] = [](float v, float min, float max) {
		return v < min ? min : (v > max ? max : v);
	};
	lua["smoothstep"] = [](float edge0, float edge1, float x) {
		float t = (x - edge0) / (edge1 - edge0);
		t = t < 0.0F ? 0.0F : (t > 1.0F ? 1.0F : t);
		return t * t * (3.0F - 2.0F * t);
	};

	// Factory function for creating paths (more intuitive than calling the type)
	lua["Path"] = []() { return GeneratedPath{}; };

	LOG_DEBUG(Engine, "Lua API bindings registered");
}

void LuaEngine::setupSandbox() {
	// Remove potentially dangerous functions for modding safety
	(*m_lua)["os"] = sol::lua_nil;
	(*m_lua)["io"] = sol::lua_nil;
	(*m_lua)["loadfile"] = sol::lua_nil;
	(*m_lua)["dofile"] = sol::lua_nil;
	(*m_lua)["debug"] = sol::lua_nil;
	(*m_lua)["package"] = sol::lua_nil;
	(*m_lua)["require"] = sol::lua_nil;

	LOG_DEBUG(Engine, "Lua sandbox configured");
}

bool LuaEngine::executeGenerator(
	const std::string& scriptPath,
	const GenerationContext& ctx,
	const GeneratorParams& params,
	GeneratedAsset& outAsset
) {
	if (!m_initialized) {
		m_lastError = "Lua engine not initialized";
		LOG_ERROR(Engine, "%s", m_lastError.c_str());
		return false;
	}

	try {
		// Read the script file
		std::ifstream file(scriptPath);
		if (!file.is_open()) {
			m_lastError = "Failed to open script: " + scriptPath;
			LOG_ERROR(Engine, "%s", m_lastError.c_str());
			return false;
		}

		std::stringstream buffer;
		buffer << file.rdbuf();
		std::string script = buffer.str();

		// Set up the context for this execution
		(*m_lua)["seed"] = ctx.seed;
		(*m_lua)["variantIndex"] = ctx.variantIndex;

		// Seed the random number generator using sol2's type-safe method
		// (avoids string concatenation which could be a code injection risk)
		(*m_lua)["math"]["randomseed"](ctx.seed + ctx.variantIndex);

		// Create helper functions to access params
		// NOTE: These lambdas capture `params` and `outAsset` by reference. This is safe
		// because scripts execute synchronously within this function call. If the execution
		// model changes to async/deferred, these captures would need to be reconsidered.
		(*m_lua)["getFloat"] = [&params](const std::string& key, float defaultVal) {
			return params.getFloat(key.c_str(), defaultVal);
		};
		(*m_lua)["getString"] = [&params](const std::string& key, const std::string& defaultVal) {
			return params.getString(key.c_str(), defaultVal);
		};
		(*m_lua)["getInt"] = [&params](const std::string& key, int32_t defaultVal) {
			return params.getInt(key.c_str(), defaultVal);
		};
		(*m_lua)["getFloatRange"] = [&params](const std::string& key, float defaultMin, float defaultMax) {
			float min = 0;
			float max = 0;
			params.getFloatRange(key.c_str(), min, max, defaultMin, defaultMax);
			return std::make_tuple(min, max);
		};

		// Clear and expose the output asset
		outAsset.clear();
		(*m_lua)["asset"] = &outAsset;

		// Execute the script
		auto result = m_lua->script(script, [](lua_State*, sol::protected_function_result pfr) {
			return pfr;
		});

		if (!result.valid()) {
			sol::error err = result;
			m_lastError = std::string("Script execution failed: ") + err.what();
			LOG_ERROR(Engine, "%s", m_lastError.c_str());
			return false;
		}

		// Verify the script produced some output
		if (outAsset.paths.empty()) {
			m_lastError = "Script produced no paths";
			LOG_WARNING(Engine, "%s: %s", scriptPath.c_str(), m_lastError.c_str());
			// Not necessarily an error - script might intentionally produce nothing
		}

		LOG_DEBUG(Engine, "Script executed successfully: %s (%zu paths)", scriptPath.c_str(), outAsset.paths.size());
		return true;

	} catch (const sol::error& e) {
		m_lastError = std::string("Lua error: ") + e.what();
		LOG_ERROR(Engine, "%s", m_lastError.c_str());
		return false;
	} catch (const std::exception& e) {
		m_lastError = std::string("Exception: ") + e.what();
		LOG_ERROR(Engine, "%s", m_lastError.c_str());
		return false;
	}
}

}  // namespace engine::assets
