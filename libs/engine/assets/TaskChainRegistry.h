#pragma once

// Task Chain Registry
// Central catalog for task chain definitions loaded from XML configuration.
// Second in the config load order - depends on ActionTypeRegistry.
//
// See /docs/design/game-systems/colonists/task-chains.md for design details.

#include "assets/TaskChainDef.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace engine::assets {

/// Central registry for task chain definitions.
/// Chains are loaded from XML definition files at startup.
class TaskChainRegistry {
  public:
    /// Get the singleton registry instance
    static TaskChainRegistry& Get();

    // --- Loading ---

    /// Load task chains from an XML file
    /// @param xmlPath Path to the XML file (e.g., task-chains.xml)
    /// @return true if loading succeeded
    bool loadFromFile(const std::string& xmlPath);

    /// Clear all loaded chains
    void clear();

    // --- Queries ---

    /// Get a chain by defName
    /// @param defName The chain definition name (e.g., "Chain_PickupDeposit")
    /// @return Pointer to chain, or nullptr if not found
    [[nodiscard]] const TaskChainDef* getChain(const std::string& defName) const;

    /// Check if a chain exists
    /// @param defName The chain definition name
    /// @return true if the chain is registered
    [[nodiscard]] bool hasChain(const std::string& defName) const;

    /// Get all registered chain defNames
    /// @return Vector of all chain defNames
    [[nodiscard]] std::vector<std::string> getChainNames() const;

    /// Get all loaded chains
    /// @return Const reference to internal chain map
    [[nodiscard]] const std::unordered_map<std::string, TaskChainDef>& getAllChains() const;

    /// Get count of loaded chains
    [[nodiscard]] size_t size() const;

  private:
    TaskChainRegistry() = default;

    /// Parse a single chain from XML node
    bool parseChainFromNode(const void* node); // pugi::xml_node passed as void*

    // --- Storage ---

    /// All loaded chains by defName
    std::unordered_map<std::string, TaskChainDef> m_chains;
};

} // namespace engine::assets
