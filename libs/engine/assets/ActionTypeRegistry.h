#pragma once

// Action Type Registry
// Central catalog for action type definitions loaded from XML configuration.
// First in the config load order - has no dependencies on other config files.
//
// See /docs/design/game-systems/colonists/task-chains.md for design details.

#include "assets/ActionTypeDef.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace engine::assets {

/// Central registry for action type definitions.
/// Actions are loaded from XML definition files at startup.
class ActionTypeRegistry {
  public:
    /// Get the singleton registry instance
    static ActionTypeRegistry& Get();

    // --- Loading ---

    /// Load action types from an XML file
    /// @param xmlPath Path to the XML file (e.g., action-types.xml)
    /// @return true if loading succeeded
    bool loadFromFile(const std::string& xmlPath);

    /// Clear all loaded action types
    void clear();

    // --- Queries ---

    /// Get an action type by defName
    /// @param defName The action definition name (e.g., "Eat", "Pickup")
    /// @return Pointer to action type, or nullptr if not found
    [[nodiscard]] const ActionTypeDef* getAction(const std::string& defName) const;

    /// Check if an action type exists
    /// @param defName The action definition name
    /// @return true if the action type is registered
    [[nodiscard]] bool hasAction(const std::string& defName) const;

    /// Check if an action requires free hands
    /// @param defName The action definition name
    /// @return true if the action needs hands, false if not found or doesn't need hands
    [[nodiscard]] bool actionNeedsHands(const std::string& defName) const;

    /// Get all registered action defNames
    /// @return Vector of all action defNames
    [[nodiscard]] std::vector<std::string> getActionNames() const;

    /// Get a comma-separated string of all action names (for error messages)
    /// @return String like "Eat, Drink, Sleep, Pickup, ..."
    [[nodiscard]] std::string getAvailableActionsString() const;

    /// Get count of loaded action types
    [[nodiscard]] size_t size() const;

  private:
    ActionTypeRegistry() = default;

    /// Parse a single action from XML node
    bool parseActionFromNode(const void* node); // pugi::xml_node passed as void*

    // --- Storage ---

    /// All loaded action types by defName
    std::unordered_map<std::string, ActionTypeDef> actions;
};

} // namespace engine::assets
