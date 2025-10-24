# String Hashing System

Created: 2025-10-12
Last Updated: 2025-10-12
Status: Active
Priority: **Implement Immediately**

## What Is String Hashing?

String hashing converts text strings into numbers (hashes) for fast comparisons and lookups.

**The Problem:**
```cpp
// Slow: Comparing strings character-by-character
if (strcmp(componentName, "Transform") == 0) {
    // O(n) operation - checks every character
}

// Slow: Hash table with string keys
std::unordered_map<std::string, Component> components;
auto it = components.find("Transform");  // Still does string comparison
```

**The Solution:**
```cpp
// Fast: Comparing integers
constexpr uint64_t kTransformHash = Hash("Transform");
if (componentHash == kTransformHash) {
    // O(1) operation - single integer comparison
}

// Fast: Hash table with integer keys
std::unordered_map<uint64_t, Component> components;
auto it = components.find(kTransformHash);  // Integer comparison
```

## Why It Matters

### Performance
- **String comparison**: O(n) - must check every character
- **Integer comparison**: O(1) - single CPU instruction
- **100-1000x faster** for lookups in hot paths

### Use Cases in World-Sim
1. **ECS Component Lookup**: "Transform" → TransformComponent
2. **Resource Loading**: "grass.svg" → TextureHandle
3. **Config Parsing**: JSON key lookups
4. **Event Systems**: Event type identification
5. **Debug Commands**: Command name → function

### Memory
- String: 8-50+ bytes
- Hash: 8 bytes (uint64_t)
- Saves memory in data structures

## Decision

Implement compile-time string hashing using FNV-1a algorithm.

**Key Principles:**
- Hashes computed at compile-time when possible
- Runtime hashing for dynamic strings
- 64-bit hashes to minimize collisions
- Use namespaced constants for common strings

## Implementation

### Core Hash Function

```cpp
// libs/foundation/utils/string_hash.h
#pragma once

#include <cstdint>

namespace foundation {

using StringHash = uint64_t;

// FNV-1a hash - fast and good distribution
constexpr StringHash HashString(const char* str) {
	StringHash hash = 0xcbf29ce484222325ULL;  // FNV offset basis
	while (*str) {
		hash ^= static_cast<uint64_t>(*str++);
		hash *= 0x100000001b3ULL;  // FNV prime
	}
	return hash;
}

// Helper macro for compile-time hashing
#define HASH(str) (foundation::HashString(str))

// Common hashes (compile-time constants)
namespace hashes {
	constexpr StringHash kTransform  = HASH("Transform");
	constexpr StringHash kPosition   = HASH("Position");
	constexpr StringHash kVelocity   = HASH("Velocity");
	constexpr StringHash kRenderable = HASH("Renderable");
	// Add more as needed
}

} // namespace foundation
```

### Usage Examples

#### ECS Component Lookup
```cpp
// Get component by type name
Component* GetComponent(EntityID entity, const char* typeName) {
	StringHash typeHash = HashString(typeName);

	switch (typeHash) {
		case hashes::kTransform:
			return &transforms[entity];
		case hashes::kVelocity:
			return &velocities[entity];
		default:
			return nullptr;
	}
}

// Or use hash directly
Component* transform = GetComponent(entity, HASH("Transform"));
```

#### Resource Loading
```cpp
class ResourceManager {
	std::unordered_map<StringHash, TextureHandle> textureCache;

public:
	TextureHandle LoadTexture(const char* path) {
		StringHash pathHash = HashString(path);

		// Check cache
		auto it = textureCache.find(pathHash);
		if (it != textureCache.end()) {
			return it->second;  // Already loaded
		}

		// Load and cache
		TextureHandle handle = LoadTextureFromDisk(path);
		textureCache[pathHash] = handle;
		return handle;
	}
};
```

#### Config Parsing
```cpp
void ParseConfig(const json& config) {
	for (const auto& [key, value] : config.items()) {
		StringHash keyHash = HashString(key.c_str());

		switch (keyHash) {
			case HASH("width"):
				screenWidth = value.get<int>();
				break;
			case HASH("height"):
				screenHeight = value.get<int>();
				break;
			case HASH("fullscreen"):
				fullscreen = value.get<bool>();
				break;
		}
	}
}
```

#### Event System
```cpp
enum class EventType : StringHash {
	KeyPressed   = HASH("KeyPressed"),
	MouseMoved   = HASH("MouseMoved"),
	TileClicked  = HASH("TileClicked"),
	ChunkLoaded  = HASH("ChunkLoaded")
};

void HandleEvent(EventType type, void* data) {
	switch (type) {
		case EventType::TileClicked:
			OnTileClicked(static_cast<TileClickEvent*>(data));
			break;
		// ...
	}
}
```

## Integration Points

### Foundation Library
**Location**: `libs/foundation/utils/string_hash.h`
- Core hash function
- Common hash constants
- Helper macros

### ECS System
**Location**: `libs/engine/ecs/`
- Component type identification
- System registration by name
- Entity queries by component name

### Resource Manager
**Location**: `libs/renderer/resources/`
- Asset path → handle mapping
- Fast cache lookups
- Hot-reload identification

### Config System
**Location**: `libs/engine/config/`
- JSON key lookups
- Fast config queries
- Runtime config updates

## Performance Characteristics

### Compile-Time Hashing
```cpp
constexpr StringHash hash = HASH("Transform");
// Generated assembly: just a constant load
// mov rax, 0x12345678ABCDEF12
```

### Runtime Hashing
```cpp
StringHash hash = HashString(runtimeString);
// ~10-20 cycles for typical string (< 20 chars)
// Still much faster than strcmp in loops
```

### Collision Probability
- 64-bit hash space: 18,446,744,073,709,551,616 values
- For 10,000 strings: ~0.00000003% collision chance
- Use `assert` to detect collisions in debug builds

## Best Practices

### DO
```cpp
// Use compile-time hashes for constants
constexpr StringHash kGrass = HASH("grass");

// Cache runtime hashes
StringHash typeHash = HashString(type);
for (Entity e : entities) {
	if (GetComponentHash(e) == typeHash) {
		// Use cached hash
	}
}

// Use named constants
if (hash == hashes::kTransform) { }
```

### DON'T
```cpp
// Don't rehash in loops
for (Entity e : entities) {
	if (GetComponentHash(e) == HashString("Transform")) {  // BAD!
		// Recomputes hash every iteration
	}
}

// Don't use for user-facing strings
DisplayText(HASH("Hello"));  // BAD! User sees number

// Don't rely on specific hash values
if (hash == 0x12345678) { }  // BAD! Implementation detail
```

## Collision Detection

```cpp
#ifdef DEBUG
// Track all hashed strings to detect collisions
std::unordered_map<StringHash, std::string> g_hashRegistry;

StringHash HashStringDebug(const char* str) {
	StringHash hash = HashString(str);

	auto it = g_hashRegistry.find(hash);
	if (it != g_hashRegistry.end() && it->second != str) {
		// COLLISION DETECTED!
		fprintf(stderr, "HASH COLLISION: '%s' and '%s' both hash to %llx\n",
			str, it->second.c_str(), hash);
		assert(false && "Hash collision detected");
	}

	g_hashRegistry[hash] = str;
	return hash;
}
#endif
```

## Trade-offs

**Pros:**
- Extremely fast comparisons
- Small memory footprint
- Compile-time computation
- Easy to use

**Cons:**
- Can't reverse hash to string (one-way)
- Tiny collision risk (but detectable)
- Debug strings harder (show hash, not text)

**Decision:** Benefits far outweigh drawbacks for performance-critical code.

## Alternatives Considered

### Option 1: Always Use std::string
**Rejected** - Too slow for hot paths, high memory overhead

### Option 2: String Interning
**Rejected** - More complex, still requires string comparison initially

### Option 3: 32-bit Hashes
**Rejected** - Higher collision risk, not worth the 4 bytes saved

## Implementation Status

- [ ] Core hash function in foundation
- [ ] Common hash constants
- [ ] Debug collision detection
- [ ] ECS integration
- [ ] Resource manager integration
- [ ] Config system integration
- [ ] Unit tests

## Implementation Order

1. **Phase 1: Foundation** (~30 min)
   - Implement HashString function
   - Add to foundation/utils/
   - Basic unit tests

2. **Phase 2: Common Hashes** (~15 min)
   - Define common hash constants
   - Document naming conventions

3. **Phase 3: Debug Tools** (~30 min)
   - Collision detection in debug builds
   - Hash registry for debugging

4. **Phase 4: Integration** (ongoing)
   - Use in ECS (component types)
   - Use in resource manager (asset paths)
   - Use in config system (JSON keys)

## Related Documentation

- Tech: [ECS Architecture](./ecs-architecture.md) (once created)
- Tech: [Resource Management](./resource-handles.md)
- Code: `libs/foundation/utils/string_hash.h` (once implemented)

## Notes

**String Debugging:**
When debugging, you see hashes not strings. Keep a reverse lookup table in debug builds:
```cpp
#ifdef DEBUG
const char* GetStringForHash(StringHash hash) {
	static std::unordered_map<StringHash, std::string> reverseMap;
	auto it = reverseMap.find(hash);
	return it != reverseMap.end() ? it->second.c_str() : "<unknown>";
}
#endif
```

**Consistency:**
Always use the same hash function. Don't mix hash algorithms or you'll get different values for the same string.
