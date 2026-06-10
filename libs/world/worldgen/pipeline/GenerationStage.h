#pragma once

#include "worldgen/data/GeneratedWorld.h"
#include "worldgen/data/PlanetParams.h"
#include "worldgen/grid/SphereGrid.h"

#include <threading/TaskPool.h>

#include <atomic>
#include <cstdint>
#include <functional>

namespace worldgen {

// Thrown by throwIfCancelled when the cancel flag is set.
struct CancelledException {};

struct StageContext {
    const PlanetParams&       params;
    const DerivedPlanetValues& derived;
    const SphereGrid&         grid;
    WorldData&                data;
    GeneratedWorld&            world;
    foundation::TaskPool&     pool;
    uint64_t                  stageSeed;
    std::function<void(float)> reportProgress;
    const std::atomic<bool>&  cancelRequested;
};

inline void throwIfCancelled(const StageContext& ctx) {
    if (ctx.cancelRequested.load(std::memory_order_relaxed)) {
        throw CancelledException{};
    }
}

class IGenerationStage {
  public:
    virtual ~IGenerationStage() = default;

    virtual const char* name()   const = 0;
    virtual float       weight() const = 0;
    virtual void        run(StageContext& ctx) = 0;
};

} // namespace worldgen
