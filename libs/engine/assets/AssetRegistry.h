#pragma once

// Asset Registry
// Central catalog for asset definitions loaded from XML files.
// Handles definition loading, generator invocation, and template caching.

#include "assets/AssetDefinition.h"
#include "assets/IAssetGenerator.h"

#include <vector/Types.h>

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

namespace engine::assets {

/// Central registry for asset definitions and generated templates.
/// Assets are loaded from XML definition files and can be generated on demand.
class AssetRegistry {
  public:
	/// Get the singleton registry instance
	static AssetRegistry& Get();

	/// Load asset definitions from an XML file
	/// @param xmlPath Path to the XML definitions file
	/// @return true if loading succeeded
	bool loadDefinitions(const std::string& xmlPath);

	/// Load all asset definitions from a folder recursively
	/// Scans for all *.xml files in the folder and subfolders.
	/// @param folderPath Path to the definitions folder
	/// @return Number of definitions loaded (0 if folder not found)
	size_t loadDefinitionsFromFolder(const std::string& folderPath);

	/// Get an asset definition by name
	/// @param defName The definition name (e.g., "Flora_GrassBlade")
	/// @return Pointer to definition, or nullptr if not found
	const AssetDefinition* getDefinition(const std::string& defName) const;

	/// Generate or retrieve a cached tessellated mesh template for an asset.
	/// For instanced assets (grass), this returns the single template.
	/// For complex assets, this returns the default variant.
	/// @param defName The definition name
	/// @return Pointer to tessellated mesh, or nullptr if not found/failed
	const renderer::TessellatedMesh* getTemplate(const std::string& defName);

	/// Generate an asset directly (does not cache)
	/// @param defName The definition name
	/// @param seed Random seed for generation
	/// @param outAsset Output generated asset
	/// @return true if generation succeeded
	bool generateAsset(const std::string& defName, uint32_t seed, GeneratedAsset& outAsset);

	/// Clear all loaded definitions and cached templates
	void clear();

	/// Get all loaded definition names
	std::vector<std::string> getDefinitionNames() const;

	// --- Entity Placement System API ---

	/// Get all defNames that belong to a group
	/// @param groupName The group name (e.g., "trees", "flowers")
	/// @return Vector of defNames (empty if group doesn't exist)
	[[nodiscard]] std::vector<std::string> getGroupMembers(const std::string& groupName) const;

	/// Get all group names in the registry
	/// @return Vector of all group names
	[[nodiscard]] std::vector<std::string> getGroups() const;

	/// Check if a group exists
	/// @param groupName The group name to check
	/// @return true if any asset declares membership in this group
	[[nodiscard]] bool hasGroup(const std::string& groupName) const;

	/// Set the path to shared scripts folder (for @shared/ prefix resolution)
	/// @param path Absolute path to the shared scripts folder
	void setSharedScriptsPath(const std::filesystem::path& path);

  private:
	AssetRegistry() = default;

	/// Tessellate a generated asset into a mesh
	bool tessellateAsset(const GeneratedAsset& asset, renderer::TessellatedMesh& outMesh);

	/// Build group index from loaded definitions
	void buildGroupIndex();

	std::unordered_map<std::string, AssetDefinition>		   definitions;
	std::unordered_map<std::string, renderer::TessellatedMesh> templateCache;

	// Group index: group name → list of defNames that belong to it
	std::unordered_map<std::string, std::vector<std::string>> groupIndex;

	// Path to shared scripts folder (for @shared/ prefix resolution)
	std::filesystem::path m_sharedScriptsPath;
};

}  // namespace engine::assets
