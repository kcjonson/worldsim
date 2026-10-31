#include "worldgen/tectonics/TectonicHistory.h"

#include <utils/WorldHash.h>

namespace worldgen::tectonics {

// FNV-1a over every per-tile array in fixed order, then the plate state. Mirrors
// the WorldData worldHash idiom (foundation::hashSpan + hashCombine).
uint64_t computeTectonicHistoryHash(const TectonicHistory& h) {
    uint64_t hash = foundation::kFnvOffset;
    hash = foundation::hashCombine(hash, foundation::hashSpan(h.plateId));
    hash = foundation::hashCombine(hash, foundation::hashSpan(h.crustType));
    hash = foundation::hashCombine(hash, foundation::hashSpan(h.crustAge));
    hash = foundation::hashCombine(hash, foundation::hashSpan(h.thicknessKm));
    hash = foundation::hashCombine(hash, foundation::hashSpan(h.orogenyAge));
    hash = foundation::hashCombine(hash, foundation::hashSpan(h.orogenyIntensity));
    hash = foundation::hashCombine(hash, foundation::hashSpan(h.volcanism));
    hash = foundation::hashCombine(hash, foundation::hashSpan(h.boundaryType));
    hash = foundation::hashCombine(hash, foundation::hashSpan(h.boundarySide));
    hash = foundation::hashCombine(hash, foundation::hashSpan(h.convergence));
    // Plate state: id, crust flag, pole, omega, rotation, area.
    for (const auto& p : h.plates) {
        hash = foundation::hashCombine(hash, foundation::hashBytes(&p.id, sizeof(p.id)));
        uint8_t cont = p.isContinental ? 1u : 0u;
        hash = foundation::hashCombine(hash, foundation::hashBytes(&cont, sizeof(cont)));
        hash = foundation::hashCombine(hash, foundation::hashBytes(&p.eulerPole, sizeof(p.eulerPole)));
        hash = foundation::hashCombine(hash, foundation::hashBytes(&p.omegaRadPerMyr, sizeof(p.omegaRadPerMyr)));
        hash = foundation::hashCombine(hash, foundation::hashBytes(p.rotation, sizeof(p.rotation)));
        hash = foundation::hashCombine(hash, foundation::hashBytes(&p.area, sizeof(p.area)));
    }
    return hash;
}

} // namespace worldgen::tectonics
