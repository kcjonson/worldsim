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
		/// @param path Path to the shared scripts folder (should be absolute for reliable resolution)
		void setSharedScriptsPath(const std::filesystem::path& path);

		// --- String Interning API (for memory-efficient entity storage) ---

		/// Get the numeric ID for a defName (for memory-efficient storage)
		/// @param defName The definition name
		/// @return ID (0 if not found - 0 is reserved as invalid ID)
		[[nodiscard]] uint32_t getDefNameId(const std::string& defName) const;

		/// Get the defName string for a numeric ID
		/// @param id The ID returned by getDefNameId
		/// @return Reference to the defName string, or empty string if invalid
		[[nodiscard]] const std::string& getDefName(uint32_t id) const;

		/// Get all capability types for a defName by ID (for capability indexing)
		/// Returns a bitmask where bit N is set if capability N is present.
		/// @param id The defName ID
		/// @return Capability bitmask (0 if not found)
		[[nodiscard]] uint8_t getCapabilityMask(uint32_t id) const;

		/// Check if a defName ID has a specific capability
		[[nodiscard]] bool hasCapability(uint32_t id, CapabilityType capability) const;

		/// Register a synthetic definition for terrain features (water tiles, etc.)
		/// These don't have XML definitions but need to participate in the capability system.
		/// @param defName The definition name (e.g., "Terrain_Water")
		/// @param capabilityMask Bitmask of capabilities
		/// @return The assigned defNameId, or 0 if registration failed
		uint32_t registerSyntheticDefinition(const std::string& defName, uint8_t capabilityMask);

		/// Get the total number of capability types
		static constexpr size_t kCapabilityTypeCount = 5;

	  private:
		AssetRegistry() = default;

		/// Tessellate a generated asset into a mesh
		bool tessellateAsset(const GeneratedAsset& asset, renderer::TessellatedMesh& outMesh);

		/// Build group index from loaded definitions
		void buildGroupIndex();

		/// Build string interning index from loaded definitions
		void buildDefNameIndex();

		std::unordered_map<std::string, AssetDefinition>		   definitions;
		std::unordered_map<std::string, renderer::TessellatedMesh> templateCache;

		// Group index: group name → list of defNames that belong to it
		std::unordered_map<std::string, std::vector<std::string>> groupIndex;

		// String interning: defName ↔ ID mapping for memory-efficient storage
		// ID 0 is reserved as "invalid/not found"
		std::unordered_map<std::string, uint32_t> m_defNameToId;
		std::vector<std::string>				  m_idToDefName; // Index 0 = empty string (invalid)

		// Pre-computed capability masks by ID (for O(1) capability checks)
		std::vector<uint8_t> m_capabilityMasks;

		// Path to shared scripts folder (for @shared/ prefix resolution)
		std::filesystem::path m_sharedScriptsPath;
	};

} // namespace engine::assets
