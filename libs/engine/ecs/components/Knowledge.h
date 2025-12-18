#pragma once

// Knowledge Component - Permanent Per-Colonist Discovery Tracking
//
// Unlike Memory (which tracks entity INSTANCES with LRU eviction),
// Knowledge tracks which TYPES of things the colonist has ever seen.
// This is permanent and never evicted - once you know what "Rock" is, you always know.
//
// Used for recipe unlocking: a recipe unlocks when the colonist knows all its inputs.
// See /docs/design/game-systems/colonists/technology-discovery.md for design details.

#include <cstdint>
#include <unordered_set>
#include <vector>

namespace ecs {

/// Knowledge component - permanent record of what a colonist has discovered.
/// Unlike Memory, this is:
/// - Type-level (defNameIds, not instances)
/// - Permanent (no eviction)
/// - Cumulative (only grows)
struct Knowledge {
    /// All things this colonist has ever seen (permanent, no eviction)
    /// Stores defNameIds from AssetRegistry::getDefNameId()
    std::unordered_set<uint32_t> knownDefs;

    // --- Query Methods ---

    /// Check if colonist knows a specific thing type
    /// @param defNameId Asset definition ID from AssetRegistry::getDefNameId()
    [[nodiscard]] bool knows(uint32_t defNameId) const {
        return defNameId != 0 && knownDefs.count(defNameId) > 0;
    }

    /// Check if colonist knows ALL items in a list (for recipe unlock checking)
    /// @param defNameIds List of definition IDs to check
    /// @return true if colonist knows every item in the list
    [[nodiscard]] bool knowsAll(const std::vector<uint32_t>& defNameIds) const {
        for (uint32_t id : defNameIds) {
            if (!knows(id)) {
                return false;
            }
        }
        return true;
    }

    // --- Mutation Methods ---

    /// Learn about a new thing type (idempotent - safe to call multiple times)
    /// @param defNameId Asset definition ID from AssetRegistry::getDefNameId()
    /// @return true if this was a NEW discovery, false if already known
    bool learn(uint32_t defNameId) {
        if (defNameId == 0) {
            return false;
        }
        auto [_, inserted] = knownDefs.insert(defNameId);
        return inserted;
    }

    /// Clear all knowledge (for testing or reset)
    void clear() {
        knownDefs.clear();
    }

    // --- Statistics ---

    /// Get count of known thing types
    [[nodiscard]] size_t count() const {
        return knownDefs.size();
    }

    /// Check if any knowledge has been acquired
    [[nodiscard]] bool empty() const {
        return knownDefs.empty();
    }
};

} // namespace ecs
