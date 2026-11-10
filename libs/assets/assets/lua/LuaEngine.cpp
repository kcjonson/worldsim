// Lua Engine Implementation
// Initializes sol2 Lua state and registers asset generation API bindings.

#include "assets/lua/LuaEngine.h"

#include <graphics/Color.h>
#include <math/Types.h>
#include <random/HashNoise.h>
#include <utils/Log.h>

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include <cmath>
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace assets {

	namespace {

		// foundation::gradientNoise2 with its lattice wrapped every `period` cells on both axes, so
		// the field repeats exactly every `period` units of x and y. In cells 0 to period - 2, where
		// no corner index changes, it equals gradientNoise2 bit for bit (same hash, gradients, fade).
		float periodicGradientNoise2(float x, float y, int32_t period, uint32_t seed) {
			int32_t ix = static_cast<int32_t>(x);
			if (static_cast<float>(ix) > x) {
				--ix;
			}
			int32_t iy = static_cast<int32_t>(y);
			if (static_cast<float>(iy) > y) {
				--iy;
			}

			const float fx = x - static_cast<float>(ix);
			const float fy = y - static_cast<float>(iy);
			const float ux = foundation::detail::quintic(fx);
			const float uy = foundation::detail::quintic(fy);

			// The remainder first, then period only when it's negative: i % period + period overflows int32 for periods near its max.
			const auto wrap = [period](int32_t i) {
				const int32_t r = i % period;
				return r < 0 ? r + period : r;
			};
			const int32_t x0 = wrap(ix);
			const int32_t x1 = wrap(ix + 1);
			const int32_t y0 = wrap(iy);
			const int32_t y1 = wrap(iy + 1);

			const auto corner = [seed](int32_t cx, int32_t cy, float dx, float dy) {
				float gx{}, gy{}, gz{};
				foundation::detail::gradient(foundation::hash3(cx, cy, 0, seed), gx, gy, gz);
				return gx * dx + gy * dy;
			};
			const float g00 = corner(x0, y0, fx, fy);
			const float g10 = corner(x1, y0, fx - 1.0F, fy);
			const float g01 = corner(x0, y1, fx, fy - 1.0F);
			const float g11 = corner(x1, y1, fx - 1.0F, fy - 1.0F);

			const float top	   = foundation::detail::lerp(g00, g10, ux);
			const float bottom = foundation::detail::lerp(g01, g11, ux);
			return foundation::detail::lerp(top, bottom, uy);
		}

		// Lua numbers reach us as doubles; a seed is whatever integer they hold, wrapped to 32 bits.
		uint32_t toSeed(double seed) {
			return static_cast<uint32_t>(static_cast<int64_t>(seed));
		}

		// How far a periodicNoise2 period may sit off a whole number: P / F in floating point lands a hair
		// off one (2.1 / 0.7 is 3.0000000000000004).
		constexpr double kPeriodTolerance = 1e-6;

	} // namespace

	LuaEngine::LuaEngine() = default;
	LuaEngine::~LuaEngine() = default;

	LuaEngine::LuaEngine(LuaEngine&&) noexcept = default;
	LuaEngine& LuaEngine::operator=(LuaEngine&&) noexcept = default;

	PreludeFiles LuaEngine::preludeFiles(const std::filesystem::path& dir) {
		namespace fs = std::filesystem;
		PreludeFiles	result;
		std::error_code ec;
		const auto		fail = [&result, &dir, &ec](const char* what) {
			   result.files.clear();
			   result.error = std::string("Failed to ") + what + " prelude folder " + dir.string() + ": " + ec.message();
			   return result;
		};
		if (dir.empty()) {
			return result;
		}
		const fs::file_status status = fs::status(dir, ec);
		// MSVC reports a missing path through ec as well as file_type::not_found; libstdc++ and libc++ only the type.
		if (status.type() == fs::file_type::not_found || ec == std::errc::no_such_file_or_directory) {
			return result;
		}
		if (ec) {
			return fail("check");
		}
		if (status.type() != fs::file_type::directory) {
			result.error = "Prelude path is not a folder: " + dir.string();
			return result;
		}
		fs::directory_iterator it(dir, ec);
		if (ec) {
			return fail("open");
		}
		for (; it != fs::directory_iterator(); it.increment(ec)) {
			if (ec) {
				break;
			}
			const bool regular = it->is_regular_file(ec);
			if (ec) {
				break;
			}
			if (regular && it->path().extension() == ".lua") {
				result.files.push_back(it->path());
			}
		}
		if (ec) {
			return fail("read");
		}
		std::sort(result.files.begin(), result.files.end(), [](const fs::path& a, const fs::path& b) { return a.filename() < b.filename(); });
		return result;
	}

	bool LuaEngine::initialize(std::filesystem::path preludeDir) {
		if (m_initialized) {
			return true;
		}
		m_preludeDir = std::move(preludeDir);

		try {
			m_lua = std::make_unique<sol::state>();
			m_lua->open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table);

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
		// NOTE: Using sol::property with lambdas instead of direct member pointers
		// for cross-platform compatibility (clang on Linux has issues with direct binding)
		m_lua->new_usertype<Vec2>(
			"Vec2",
			sol::constructors<Vec2(), Vec2(float, float)>(),
			"x",
			sol::property([](Vec2& v) { return v.x; }, [](Vec2& v, float val) { v.x = val; }),
			"y",
			sol::property([](Vec2& v) { return v.y; }, [](Vec2& v, float val) { v.y = val; }),
			// Arithmetic operations
			sol::meta_function::addition,
			[](const Vec2& a, const Vec2& b) { return Vec2{a.x + b.x, a.y + b.y}; },
			sol::meta_function::subtraction,
			[](const Vec2& a, const Vec2& b) { return Vec2{a.x - b.x, a.y - b.y}; },
			sol::meta_function::multiplication,
			sol::overload(
				[](const Vec2& v, float s) { return Vec2{v.x * s, v.y * s}; }, [](float s, const Vec2& v) { return Vec2{v.x * s, v.y * s}; }
			),
			// Utility functions
			"length",
			[](const Vec2& v) { return std::sqrt(v.x * v.x + v.y * v.y); },
			"normalize",
			[](const Vec2& v) {
				float len = std::sqrt(v.x * v.x + v.y * v.y);
				if (len > 0.0001F) {
					return Vec2{v.x / len, v.y / len};
				}
				return v;
			},
			"dot",
			[](const Vec2& a, const Vec2& b) { return a.x * b.x + a.y * b.y; }
		);

		// Register Color type
		m_lua->new_usertype<Color>(
			"Color",
			sol::constructors<Color(), Color(float, float, float, float)>(),
			"r",
			sol::property([](Color& c) { return c.r; }, [](Color& c, float val) { c.r = val; }),
			"g",
			sol::property([](Color& c) { return c.g; }, [](Color& c, float val) { c.g = val; }),
			"b",
			sol::property([](Color& c) { return c.b; }, [](Color& c, float val) { c.b = val; }),
			"a",
			sol::property([](Color& c) { return c.a; }, [](Color& c, float val) { c.a = val; }),
			// Factory functions
			"rgb",
			[](float r, float g, float b) { return Color(r, g, b, 1.0F); },
			"rgba",
			[](float r, float g, float b, float a) { return Color(r, g, b, a); },
			// Utility
			"lerp",
			[](const Color& a, const Color& b, float t) {
				return Color(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t);
			}
		);

		// Register GeneratedPath type
		// NOTE: Using sol::property with lambdas instead of direct member pointers
		// for cross-platform compatibility (clang on Linux has issues with direct binding)
		m_lua->new_usertype<GeneratedPath>(
			"Path",
			sol::constructors<GeneratedPath()>(),
			"vertices",
			sol::property(
				[](GeneratedPath& p) -> std::vector<Vec2>& { return p.vertices; },
				[](GeneratedPath& p, const std::vector<Vec2>& v) { p.vertices = v; }
			),
			"fillColor",
			sol::property(
				[](GeneratedPath& p) -> Color& { return p.fillColor; }, [](GeneratedPath& p, const Color& c) { p.fillColor = c; }
			),
			"isClosed",
			sol::property([](GeneratedPath& p) { return p.isClosed; }, [](GeneratedPath& p, bool v) { p.isClosed = v; }),
			// Convenience methods
			"addVertex",
			// A NaN or infinite coordinate is a Lua error: the tessellator has no guard, and one would spoil the mesh
			[](GeneratedPath& path, float x, float y) {
				if (!std::isfinite(x) || !std::isfinite(y)) {
					throw std::invalid_argument("addVertex: vertex coordinates must be finite numbers");
				}
				path.vertices.push_back(Vec2{x, y});
			},
			"setColor",
			[](GeneratedPath& path, float r, float g, float b, float a) { path.fillColor = Color(r, g, b, a); },
			// Draw this path only where the screen shows at least n pixels per meter (ground tiles)
			"setLod",
			[](GeneratedPath& path, float minPixelsPerMeter) { path.minPixelsPerMeter = minPixelsPerMeter; },
			// This path's height in the ground tile bake, 0 to 1
			"setHeight",
			[](GeneratedPath& path, float height) {
				if (!(height >= 0.0F && height <= 1.0F)) {
					throw std::invalid_argument("setHeight: the height must be from 0 to 1");
				}
				path.height = height;
			},
			"close",
			[](GeneratedPath& path) { path.isClosed = true; },
			"clear",
			[](GeneratedPath& path) { path.clear(); }
		);

		// Register GeneratedAsset type
		m_lua->new_usertype<GeneratedAsset>(
			"Asset",
			sol::constructors<GeneratedAsset()>(),
			"paths",
			sol::property(
				[](GeneratedAsset& a) -> std::vector<GeneratedPath>& { return a.paths; },
				[](GeneratedAsset& a, const std::vector<GeneratedPath>& p) { a.paths = p; }
			),
			// Convenience methods
			"addPath",
			[](GeneratedAsset& asset, const GeneratedPath& path) { asset.addPath(path); },
			"clear",
			[](GeneratedAsset& asset) { asset.clear(); },
			"createPath",
			[](GeneratedAsset& asset) -> GeneratedPath& {
				asset.paths.emplace_back();
				return asset.paths.back();
			},
			// Declare a rectangular collision footprint (local meters). Center
			// defaults to the asset origin. Captured eagerly at load so nav sees it.
			"setCollisionRect",
			[](GeneratedAsset& asset, float halfWidthMeters, float halfHeightMeters, float centerXMeters, float centerYMeters) {
				if (halfWidthMeters <= 0.0F || halfHeightMeters <= 0.0F) {
					LOG_WARNING(Engine, "setCollisionRect ignored: non-positive half-extents (%.3f, %.3f)", halfWidthMeters, halfHeightMeters);
					return;
				}
				CollisionShape shape;
				shape.type			   = CollisionShapeType::Rect;
				shape.offsetMeters	   = {centerXMeters, centerYMeters};
				shape.halfExtentsMeters = {halfWidthMeters, halfHeightMeters};
				asset.emittedCollision = shape;
			}
		);

		// Register utility math functions
		auto& lua = *m_lua;
		lua["lerp"] = [](float a, float b, float t) {
			return a + (b - a) * t;
		};
		lua["clamp"] = [](float v, float min, float max) {
			return v < min ? min : (v > max ? max : v);
		};
		lua["smoothstep"] = [](float edge0, float edge1, float x) {
			float t = (x - edge0) / (edge1 - edge0);
			t = t < 0.0F ? 0.0F : (t > 1.0F ? 1.0F : t);
			return t * t * (3.0F - 2.0F * t);
		};

		// Seeded gradient noise, about -1 to 1, zero at integer lattice points. Same inputs, same
		// value on every platform. Scale x and y by 1/featureSize to pick the feature size.
		lua["noise2"] = [](float x, float y, double seed) {
			return foundation::gradientNoise2(x, y, toSeed(seed));
		};
		// noise2 whose lattice repeats every `period` cells (a whole number >= 1), so
		// periodicNoise2(x + period, y, period, seed) == periodicNoise2(x, y, period, seed), and the
		// same along y. Scale x and y into lattice cells first; a ground tile of P meters with
		// features of F meters calls it as periodicNoise2(x / F, y / F, P / F, seed). The period
		// arrives as a double because P / F is a float in Lua even when it divides evenly, and one
		// within kPeriodTolerance of a whole number is taken as that number.
		lua["periodicNoise2"] = [](float x, float y, double period, double seed) {
			const double whole = std::round(period);
			if (!(std::abs(period - whole) < kPeriodTolerance) || whole < 1.0 ||
				whole > static_cast<double>(std::numeric_limits<int32_t>::max())) {
				throw std::invalid_argument("periodicNoise2: period must be a whole number >= 1");
			}
			return periodicGradientNoise2(x, y, static_cast<int32_t>(whole), toSeed(seed));
		};

		// Factory function for creating paths - enables Path() syntax instead of Path.new()
		// NOTE: This is intentional. sol2's new_usertype registers types as tables callable
		// via Type.new(), but Path() is more intuitive for Lua scripts. Keep this factory.
		lua["Path"] = []() {
			return GeneratedPath{};
		};

		LOG_DEBUG(Engine, "Lua API bindings registered");
	}

	void LuaEngine::setupSandbox() {
		// Sandbox Lua environment for safe asset generation scripts.
		// Scripts can only use math, string manipulation, and our registered APIs.
		// Removed capabilities:
		// - os/io: Filesystem access could read/write arbitrary files
		// - loadfile/dofile: Could execute untrusted code from disk
		// - debug: Allows introspection/modification of Lua internals
		// - package/require: Could load external modules with arbitrary code
		(*m_lua)["os"] = sol::lua_nil;
		(*m_lua)["io"] = sol::lua_nil;
		(*m_lua)["loadfile"] = sol::lua_nil;
		(*m_lua)["dofile"] = sol::lua_nil;
		(*m_lua)["debug"] = sol::lua_nil;
		(*m_lua)["package"] = sol::lua_nil;
		(*m_lua)["require"] = sol::lua_nil;

		LOG_DEBUG(Engine, "Lua sandbox configured");
	}

	bool LuaEngine::runPrelude() {
		const PreludeFiles prelude = preludeFiles(m_preludeDir);
		if (!prelude.error.empty()) {
			m_lastError = prelude.error;
			LOG_ERROR(Engine, "%s", m_lastError.c_str());
			return false;
		}
		for (const std::filesystem::path& file : prelude.files) {
			std::ifstream in(file, std::ios::binary);
			if (!in.is_open()) {
				m_lastError = "Failed to open prelude file: " + file.string();
				LOG_ERROR(Engine, "%s", m_lastError.c_str());
				return false;
			}
			std::stringstream buffer;
			buffer << in.rdbuf();
			auto result = m_lua->script(buffer.str(), [](lua_State*, sol::protected_function_result pfr) { return pfr; }, "@" + file.string());
			if (!result.valid()) {
				sol::error err = result;
				m_lastError = "Prelude file " + file.string() + " failed: " + err.what();
				LOG_ERROR(Engine, "%s", m_lastError.c_str());
				return false;
			}
		}
		return true;
	}

	bool LuaEngine::executeGenerator(
		const std::string&		 scriptPath,
		const GenerationContext& ctx,
		const GeneratorParams&	 params,
		GeneratedAsset&			 outAsset
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
			if (ctx.periodMeters.has_value()) {
				(*m_lua)["period"] = *ctx.periodMeters;
			} else {
				(*m_lua)["period"] = sol::lua_nil;
			}

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

			if (!runPrelude()) {
				return false;
			}

			// Execute the script
			auto result = m_lua->script(script, [](lua_State*, sol::protected_function_result pfr) { return pfr; });

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

} // namespace assets
