#pragma once

// Task Chain Registry
// Central catalog for task chain definitions loaded from XML configuration.
// Second in the config load order - depends on ActionTypeRegistry.
//
// See /docs/design/game-systems/colonists/task-chains.md for design details.

#include "assets/TaskChainDef.h"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace assets {

/// Central registry for task chain definitions.
/// Chains are loaded from XML definition files at startup.
class TaskChainRegistry {
  public:
    /// Get the singleton registry instance
    static TaskChainRegistry& Get();

    // --- Loading ---

    /// Replaces every chain with those in `configRoot / file`: false, with problems() saying why and no chains
    /// held, when the file can't be read or has any mistake. Errors name the file as `file` is written.
    bool loadFromFile(const std::filesystem::path& configRoot, const std::filesystem::path& file);

    /// What the file got wrong: each names the file, the <Chain> and the step or attribute.
    [[nodiscard]] const std::vector<std::string>& problems() const { return problemList; }

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

    // --- Storage ---

    /// All loaded chains by defName
    std::unordered_map<std::string, TaskChainDef> chains;

    std::vector<std::string> problemList;
};

} // namespace assets
