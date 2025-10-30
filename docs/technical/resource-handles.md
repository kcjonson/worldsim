# Handle-Based Resource Management

Created: 2025-10-12
Last Updated: 2025-10-12
Status: Active
Priority: **Implement Soon** (before SVG asset system)

## What Is a Resource Handle?

A handle is a safe ID number that refers to a resource (texture, mesh, sound, etc.) instead of a raw pointer.

**Think of it like:** A ticket number at a deli counter. The number is valid even if staff changes, and you can check if your order is ready.

## The Problem

```cpp
// Using raw pointers - DANGEROUS!
Texture* grassTexture = LoadTexture("grass.svg");

// ... later, texture gets unloaded ...
UnloadTexture(grassTexture);

// ... somewhere else, still using pointer ...
grassTexture->Bind();  // CRASH! Dangling pointer

// Problems:
// - Can't tell if pointer is still valid
// - Hot-reloading breaks all pointers
// - Hard to serialize/save (pointer addresses change)
// - Memory errors hard to debug
```

## The Solution

```cpp
// Using handles - SAFE!
TextureHandle grassHandle = LoadTexture("grass.svg");

// ... texture gets unloaded ...
UnloadTexture(grassHandle);

// ... somewhere else, using handle ...
Texture* tex = GetTexture(grassHandle);
if (tex) {
	tex->Bind();  // Safe! Returns null if invalid
} else {
	// Handle is invalid, load fallback
}

// Benefits:
// - Can validate handle before use
// - Hot-reload works (handle stays valid)
// - Easy to serialize (just save 32-bit number)
// - Clear ownership semantics
```

## Why It Matters

### Safety
- **No dangling pointers**: Can detect if resource was freed
- **Hot-reloading**: Reload asset, handles still work
- **Clear lifetime**: Know when resource can be freed

### Memory
- **Compact**: 32-bit handle vs 64-bit pointer
- **Serializable**: Save/load handles in save files

### Use Cases in World-Sim
1. **SVG Assets**: grass.svg → TextureHandle
2. **Rasterized Tiles**: Cached rasters
3. **Meshes**: Chunk geometry
4. **Sounds**: Audio clips (future)

## Implementation

### Core Handle Type

```cpp
// libs/renderer/resources/resource_handle.h
#pragma once

#include <cstdint>

namespace renderer {

// 32-bit handle: 16-bit index + 16-bit generation
struct ResourceHandle {
	uint32_t value;

	static constexpr uint32_t kInvalidHandle = 0xFFFFFFFF;

	bool IsValid() const { return value != kInvalidHandle; }

	uint16_t GetIndex() const { return value & 0xFFFF; }
	uint16_t GetGeneration() const { return value >> 16; }

	static ResourceHandle Make(uint16_t index, uint16_t generation) {
		return { (static_cast<uint32_t>(generation) << 16) | index };
	}

	static ResourceHandle Invalid() {
		return { kInvalidHandle };
	}

	bool operator==(ResourceHandle other) const { return value == other.value; }
	bool operator!=(ResourceHandle other) const { return value != other.value; }
};

// Type-safe handles
using TextureHandle = ResourceHandle;
using MeshHandle = ResourceHandle;
using SVGAssetHandle = ResourceHandle;

} // namespace renderer
```

### Resource Manager Template

```cpp
// libs/renderer/resources/resource_manager.h
#pragma once

#include "resource_handle.h"
#include <vector>
#include <cassert>

namespace renderer {

template<typename T>
class ResourceManager {
public:
	ResourceManager(size_t capacity = 1024) {
		m_resources.reserve(capacity);
		m_generations.reserve(capacity);
		m_freeIndices.reserve(capacity);
	}

	// Allocate new resource
	ResourceHandle Allocate() {
		uint16_t index;

		if (!m_freeIndices.empty()) {
			// Reuse freed slot
			index = m_freeIndices.back();
			m_freeIndices.pop_back();
		} else {
			// Allocate new slot
			index = static_cast<uint16_t>(m_resources.size());
			m_resources.emplace_back();
			m_generations.push_back(0);
		}

		return ResourceHandle::Make(index, m_generations[index]);
	}

	// Free resource
	void Free(ResourceHandle handle) {
		uint16_t index = handle.GetIndex();
		assert(index < m_resources.size());

		// Increment generation (invalidates old handles)
		m_generations[index]++;

		// Add to free list
		m_freeIndices.push_back(index);
	}

	// Get resource (validates handle)
	T* Get(ResourceHandle handle) {
		uint16_t index = handle.GetIndex();
		if (index >= m_resources.size()) {
			return nullptr;
		}

		// Check generation
		if (handle.GetGeneration() != m_generations[index]) {
			return nullptr;  // Stale handle
		}

		return &m_resources[index];
	}

	// Get resource (const)
	const T* Get(ResourceHandle handle) const {
		return const_cast<ResourceManager*>(this)->Get(handle);
	}

private:
	std::vector<T>        m_resources;
	std::vector<uint16_t> m_generations;
	std::vector<uint16_t> m_freeIndices;
};

} // namespace renderer
```

## Usage Examples

### Texture Manager

```cpp
// libs/renderer/resources/texture_manager.h
#pragma once

#include "resource_manager.h"
#include <string>
#include <unordered_map>

struct Texture {
	GLuint id;
	int width;
	int height;
};

class TextureManager {
public:
	TextureHandle LoadTexture(const char* path) {
		// Check if already loaded
		StringHash pathHash = HashString(path);
		auto it = m_pathToHandle.find(pathHash);
		if (it != m_pathToHandle.end()) {
			return it->second;  // Already loaded
		}

		// Allocate handle
		TextureHandle handle = m_textures.Allocate();
		Texture* tex = m_textures.Get(handle);

		// Load texture data
		tex->id = LoadTextureFromDisk(path);
		tex->width = /* ... */;
		tex->height = /* ... */;

		// Cache path → handle
		m_pathToHandle[pathHash] = handle;

		return handle;
	}

	void UnloadTexture(TextureHandle handle) {
		Texture* tex = m_textures.Get(handle);
		if (tex) {
			glDeleteTextures(1, &tex->id);
			m_textures.Free(handle);
		}
	}

	Texture* GetTexture(TextureHandle handle) {
		return m_textures.Get(handle);
	}

private:
	ResourceManager<Texture> m_textures;
	std::unordered_map<StringHash, TextureHandle> m_pathToHandle;
};
```

### Using Textures

```cpp
// Load texture
TextureHandle grassHandle = texManager.LoadTexture("assets/grass.svg");

// Use texture
void RenderTile(TextureHandle texHandle) {
	Texture* tex = texManager.GetTexture(texHandle);
	if (tex) {
		glBindTexture(GL_TEXTURE_2D, tex->id);
		// Draw tile...
	} else {
		// Handle invalid, use fallback
		glBindTexture(GL_TEXTURE_2D, fallbackTexture);
	}
}

// Hot-reload texture
void HotReloadTexture(const char* path) {
	TextureHandle handle = texManager.GetHandleForPath(path);
	Texture* tex = texManager.GetTexture(handle);
	if (tex) {
		glDeleteTextures(1, &tex->id);
		tex->id = LoadTextureFromDisk(path);  // Reload
		// Handle still valid! All references still work
	}
}
```

### SVG Asset Manager

```cpp
class SVGAssetManager {
public:
	SVGAssetHandle LoadSVG(const char* path) {
		SVGAssetHandle handle = m_assets.Allocate();
		SVGAsset* asset = m_assets.Get(handle);

		asset->data = ParseSVG(path);
		asset->bounds = CalculateBounds(asset->data);

		return handle;
	}

	void UnloadSVG(SVGAssetHandle handle) {
		SVGAsset* asset = m_assets.Get(handle);
		if (asset) {
			FreeSVGData(asset->data);
			m_assets.Free(handle);
		}
	}

	SVGAsset* GetSVG(SVGAssetHandle handle) {
		return m_assets.Get(handle);
	}

private:
	ResourceManager<SVGAsset> m_assets;
};
```

## How Generations Work

```cpp
// Initial state
index=0, gen=0: Grass texture loaded
Handle(index=0, gen=0) → Valid

// Unload texture
m_generations[0]++;  // Now gen=1
Free index 0

// Old handle still exists somewhere
Handle(index=0, gen=0) → Invalid! (gen mismatch)

// Allocate new texture in same slot
index=0, gen=1: Dirt texture loaded
Handle(index=0, gen=1) → Valid

// Old grass handle still invalid
Handle(index=0, gen=0) → Invalid! (gen=0 != gen=1)
```

## When to Use Handles

### DO Use For
- Textures, meshes, shaders
- SVG assets and rasterized caches
- Sounds, music (future)
- Any resource that can be hot-reloaded
- Any resource referenced in multiple places

### DON'T Use For
- Local temporary resources
- Resources with single owner
- When pointer is always valid (const resources)

## Best Practices

```cpp
// Good: Check validity before use
Texture* tex = texManager.GetTexture(handle);
if (tex) {
	UseTexture(tex);
} else {
	UseFallbackTexture();
}

// Bad: Assume handle is valid
Texture* tex = texManager.GetTexture(handle);
tex->Bind();  // Could be null!

// Good: Store handles, not pointers
struct Tile {
	TextureHandle texture;  // Safe
};

// Bad: Store pointers
struct Tile {
	Texture* texture;  // Dangerous!
};
```

## Performance

**Handle Operations:**
- Allocate: O(1) - pop from free list
- Free: O(1) - push to free list, increment generation
- Get: O(1) - array lookup + generation check

**Memory:**
- Handle: 4 bytes (32-bit)
- Pointer: 8 bytes (64-bit)
- Saves 50% memory for resource references!

## Trade-offs

**Pros:**
- Safe against dangling pointers
- Hot-reloading works seamlessly
- Compact (4 bytes vs 8)
- Serializable
- Clear ownership

**Cons:**
- Extra indirection (array lookup)
- Must check validity before use
- 16-bit limit (65536 resources max per type)
- Slightly more complex code

**Decision:** Essential for robust resource management and hot-reloading.

## Related Documentation

- Tech: [Vector Asset Pipeline](./vector-asset-pipeline.md)
- Tech: [String Hashing](./string-hashing.md)
- Code: `libs/renderer/resources/` (once implemented)

## Notes

**Handle Limits:** 16-bit index = 65536 max resources per type. If you need more, use 32-bit index with smaller generation field.

**Thread Safety:** Basic implementation not thread-safe. Add mutex if accessing from multiple threads.

**Weak References:** Handles are like weak pointers - they can become invalid. Always check before use!
