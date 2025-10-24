# World Generation Architecture

Created: 2025-10-12
Last Updated: 2025-10-12
Status: Active

## Context

The world generation system needs to create 3D spherical planets with various terrain features. The initial implementation will use a simple Perlin noise generator, but this is temporary and will be replaced with more sophisticated generators in the future.

**Key Requirements:**
- Pluggable generator architecture (easy to swap implementations)
- 3D spherical world (not flat)
- Generates terrain data that can be sampled into 2D tiles
- Progress reporting during generation (can take time)

## Decision

Use an abstract generator interface with pluggable implementations.

### Generator Interface

```cpp
namespace worldgen {

struct WorldParams {
    int seed;
    float radius;
    // ... more parameters based on generator type
    nlohmann::json customParams; // Generator-specific params
};

struct WorldData {
    float radius;
    // Terrain data stored as spherical coordinates
    // Details TBD based on implementation needs
};

class IWorldGenerator {
public:
    virtual ~IWorldGenerator() = default;

    virtual WorldData generate(
        const WorldParams& params,
        std::function<void(float progress, std::string status)> callback
    ) = 0;

    virtual std::string getName() const = 0;
    virtual std::string getDescription() const = 0;
};

} // namespace worldgen
```

### Initial Implementation: Perlin Noise Generator

```cpp
class PerlinNoiseGenerator : public IWorldGenerator {
public:
    WorldData generate(
        const WorldParams& params,
        std::function<void(float, std::string)> callback
    ) override;

    std::string getName() const override { return "Perlin Noise"; }
    std::string getDescription() const override {
        return "Simple Perlin noise terrain (temporary implementation)";
    }
};
```

**Important:** This Perlin noise generator is **temporary** and will be replaced. Code using it should:
- Use the `IWorldGenerator` interface, not concrete `PerlinNoiseGenerator`
- Be prepared for different world data formats from future generators
- Document that this is a placeholder implementation

### Generator Registry

Allow runtime selection of generators:

```cpp
class GeneratorRegistry {
public:
    static void registerGenerator(
        const std::string& name,
        std::unique_ptr<IWorldGenerator> generator
    );

    static IWorldGenerator* getGenerator(const std::string& name);
    static std::vector<std::string> listGenerators();
};
```

### File Organization

```
libs/world/
├── include/world/
│   ├── generator_interface.h      # IWorldGenerator interface
│   ├── world_data.h                # WorldData structures
│   ├── generator_registry.h       # Generator registration
│   └── generators/
│       └── perlin_generator.h     # Initial implementation
├── src/
│   ├── generator_registry.cpp
│   └── generators/
│       └── perlin_generator.cpp   # TEMPORARY - will be removed
└── README.md                       # Notes about replacing generator
```

## Implementation Details

### Progress Reporting

Generators report progress via callback:

```cpp
auto generator = GeneratorRegistry::getGenerator("Perlin Noise");
WorldData world = generator->generate(params,
    [](float progress, std::string status) {
        std::cout << status << ": " << (progress * 100) << "%" << std::endl;
    }
);
```

This allows the UI to show:
- Progress bar (0.0 to 1.0)
- Status message ("Generating terrain...", "Computing moisture...", etc.)

### Integration with 3D Preview

The world-creator scene needs to:
1. Display 3D planet while user adjusts parameters
2. Call generator when user clicks "Generate"
3. Show progress bar during generation
4. Display generated world for inspection
5. Pass world data to game scene when user clicks "Land"

### Future Generator Examples

Future generators might include:
- **Plate Tectonics Generator**: Simulate continental drift
- **Erosion-Based Generator**: Model water and wind erosion
- **Biome-Based Generator**: Define climate zones and ecosystems
- **Procedural City Generator**: For sci-fi planets with structures
- **Imported Height Map**: Load from external data

Each would implement `IWorldGenerator` with its own parameters and algorithms.

## Trade-offs

**Pros:**
- Easy to add new generators without changing existing code
- UI code doesn't depend on specific generator implementation
- Can compare different generators easily
- Clear migration path from simple to complex

**Cons:**
- Interface might need updates as requirements emerge
- Initial abstraction may be over-engineered for single generator
- WorldData structure needs to be flexible enough for all generators

**Decision:** Accept abstraction complexity upfront. Past experience shows that "temporary" solutions tend to stick around, so make replacement easy from day one.

## Alternatives Considered

### Option: Hard-code Perlin generator, refactor later
**Rejected** - Refactoring is painful when the "temporary" code spreads through the codebase. Do it right initially.

### Option: Script-based generators (Lua/Python)
**Considered for future** - Could allow user-created generators. Not needed for MVP.

### Option: GPU-based generation
**Considered for future** - For real-time previews during parameter adjustment. Start with CPU, optimize later.

## Implementation Status

- [x] Architecture designed
- [ ] Interface defined in code
- [ ] Perlin noise generator implemented
- [ ] Generator registry implemented
- [ ] Integration with world-creator scene

## Related Documentation

- Design Doc: [World Generation Feature](/docs/design/features/world-creation/generation.md)
- Tech: [3D to 2D Sampling](./3d-to-2d-sampling.md)
- Code: `libs/world/include/world/` (once implemented)

## Notes

**Migration Path from Perlin:**
When replacing the Perlin generator:
1. Implement new `IWorldGenerator`
2. Register in `GeneratorRegistry`
3. Update default generator in config
4. Remove Perlin implementation files
5. Update this document's status

Leave comments in code like:
```cpp
// TODO: Replace with proper terrain generation algorithm
// See docs/technical/world-generation-architecture.md for migration strategy
```
