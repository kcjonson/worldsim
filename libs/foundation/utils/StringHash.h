#pragma once

#include <cstdint>

#ifdef DEBUG
#include <cassert>
#include <string>
#include <unordered_map>
#endif

namespace foundation {

	// Type alias for string hashes
	using StringHash = uint64_t;

	// FNV-1a hash function - fast and good distribution
	// Can be evaluated at compile-time for string literals
	constexpr StringHash hashString(const char* str) {
		StringHash hash = 0xcbf29ce484222325ULL;   // FNV offset basis
		while (*str) {							   // NOLINT(readability-implicit-bool-conversion)
			hash ^= static_cast<uint64_t>(*str++); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
			hash *= 0x100000001b3ULL;			   // FNV prime
		}
		return hash;
	}

// Helper macro for compile-time hashing
// Usage: constexpr StringHash hash = HASH("MyString");
#define HASH(str) (foundation::hashString(str)) // NOLINT(cppcoreguidelines-macro-usage)

	// Common hashes (compile-time constants)
	// Add more as needed by different systems
	namespace hashes {
		// ECS Component types
		constexpr StringHash kTransform = HASH("Transform");
		constexpr StringHash kPosition = HASH("Position");
		constexpr StringHash kVelocity = HASH("Velocity");
		constexpr StringHash kRenderable = HASH("Renderable");

		// Common resource types
		constexpr StringHash kTexture = HASH("Texture");
		constexpr StringHash kShader = HASH("Shader");
		constexpr StringHash kMesh = HASH("Mesh");

		// Config keys
		constexpr StringHash kWidth = HASH("width");
		constexpr StringHash kHeight = HASH("height");
		constexpr StringHash kFullscreen = HASH("fullscreen");

	} // namespace hashes

#ifdef DEBUG

	// Debug-only collision detection
	// Tracks all hashed strings to detect hash collisions
	// Only available in Debug builds to avoid overhead in Release
	namespace detail {

		inline std::unordered_map<StringHash, std::string>& getHashRegistry() {
			static std::unordered_map<StringHash, std::string> registry;
			return registry;
		}

	} // namespace detail

	// Hash a string and register it for collision detection
	// In Debug builds, this will assert if a collision is detected
	inline StringHash hashStringDebug(const char* str) {
		StringHash hash = hashString(str);

		auto& registry = detail::getHashRegistry();
		auto  it = registry.find(hash);

		if (it != registry.end() && it->second != str) {
			// COLLISION DETECTED!
			fprintf(stderr, "HASH COLLISION: '%s' and '%s' both hash to %llx\n", str, it->second.c_str(), hash);
			assert(false && "Hash collision detected");
		}

		registry[hash] = str;
		return hash;
	}

	// Get the original string for a hash (for debugging)
	// Returns "<unknown>" if the hash was never registered
	inline const char* getStringForHash(StringHash hash) {
		auto& registry = detail::getHashRegistry();
		auto  it = registry.find(hash);
		return it != registry.end() ? it->second.c_str() : "<unknown>";
	}

#endif // DEBUG

} // namespace foundation

/*
 * USAGE EXAMPLES:
 *
 * Compile-time hashing (fastest):
 *   constexpr StringHash hash = HASH("MyString");
 *   if (hash == foundation::hashes::kTransform) { ... }
 *
 * Runtime hashing:
 *   const char* runtimeString = getStringFromUser();
 *   StringHash hash = foundation::hashString(runtimeString);
 *
 * Debug collision detection:
 *   #ifdef DEBUG
 *   StringHash hash = foundation::hashStringDebug("MyString");
 *   const char* original = foundation::getStringForHash(hash);
 *   #endif
 *
 * Best practices:
 *   - Use HASH() for string literals (compile-time)
 *   - Cache runtime hashes (don't rehash in loops)
 *   - Use named constants from hashes namespace
 *   - Never rely on specific hash values in code
 *   - Use hashStringDebug() in tests to catch collisions
 */
