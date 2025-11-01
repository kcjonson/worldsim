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
	constexpr StringHash HashString(const char* str) {
		StringHash hash = 0xcbf29ce484222325ULL;   // FNV offset basis
		while (*str) {							   // NOLINT(readability-implicit-bool-conversion)
			hash ^= static_cast<uint64_t>(*str++); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
			hash *= 0x100000001b3ULL;			   // FNV prime
		}
		return hash;
	}

// Helper macro for compile-time hashing
// Usage: constexpr StringHash hash = HASH("MyString");
#define HASH(str) // NOLINT(cppcoreguidelines-macro-usage) (foundation::HashString(str))

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

		inline std::unordered_map<StringHash, std::string>& GetHashRegistry() {
			static std::unordered_map<StringHash, std::string> registry;
			return registry;
		}

	} // namespace detail

	// Hash a string and register it for collision detection
	// In Debug builds, this will assert if a collision is detected
	inline StringHash HashStringDebug(const char* str) {
		StringHash hash = HashString(str);

		auto& registry = detail::GetHashRegistry();
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
	inline const char* GetStringForHash(StringHash hash) {
		auto& registry = detail::GetHashRegistry();
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
 *   const char* runtimeString = GetStringFromUser();
 *   StringHash hash = foundation::HashString(runtimeString);
 *
 * Debug collision detection:
 *   #ifdef DEBUG
 *   StringHash hash = foundation::HashStringDebug("MyString");
 *   const char* original = foundation::GetStringForHash(hash);
 *   #endif
 *
 * Best practices:
 *   - Use HASH() for string literals (compile-time)
 *   - Cache runtime hashes (don't rehash in loops)
 *   - Use named constants from hashes namespace
 *   - Never rely on specific hash values in code
 *   - Use HashStringDebug() in tests to catch collisions
 */
