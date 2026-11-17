#pragma once

// Action Type Registry
// Central catalog for action type definitions loaded from XML configuration.
// First in the config load order - has no dependencies on other config files.
//
// See /docs/design/game-systems/colonists/task-chains.md for design details.

#include "assets/ActionTypeDef.h"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace assets {

	/// Central registry for action type definitions.
	/// Actions are loaded from XML definition files at startup.
	class ActionTypeRegistry {
	  public:
		/// Get the singleton registry instance
		static ActionTypeRegistry& Get();

		// --- Loading ---

		/// Replaces every action type with those in `configRoot / file`: false, with problems() saying why and no
		/// action types held, when the file can't be read or has any mistake. Errors name the file as `file` is written.
		bool loadFromFile(const std::filesystem::path& configRoot, const std::filesystem::path& file);

		/// What the file got wrong: each names the file, the <Action> and the attribute or element.
		[[nodiscard]] const std::vector<std::string>& problems() const { return problemList; }

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

		// --- Test Support ---

		/// Register an action type directly (for unit tests), bypassing XML loading.
		/// @param action The action type to register
		void registerTestAction(ActionTypeDef action);

	  private:
		ActionTypeRegistry() = default;

		/// All loaded action types by defName
		std::unordered_map<std::string, ActionTypeDef> actions;
		std::vector<std::string>					   problemList;
	};

} // namespace assets
