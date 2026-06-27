#pragma once

// Asset Registry
// Central catalog for asset definitions loaded from XML files.
// Handles definition loading, generator invocation, and template caching.

#include "assets/AssetDefinition.h"
#include "assets/AssetValidator.h"
#include "assets/IAssetGenerator.h"
#include "assets/MotionDef.h"

#include <vector/Types.h>

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>

namespace engine::assets {

	/// Snapshot of asynchronous asset loading. All fields are atomic so the splash
	/// (main thread) can poll them while the load worker writes them.
	struct LoadProgress {
		std::atomic<bool> started{false};
		std::atomic<bool> done{false};
		std::atomic<int>  defsLoaded{0};
	};

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
		/// @param onProgress Optional callback invoked with the running definition
		///        count as files load (used by the async loader to drive the splash)
		/// @return Number of definitions loaded (0 if folder not found)
		size_t loadDefinitionsFromFolder(const std::string& folderPath, const std::function<void(int)>& onProgress = {});

		/// Get an asset definition by name
		/// @param defName The definition name (e.g., "Groundcover_Grass")
		/// @return Pointer to definition, or nullptr if not found
		const AssetDefinition* getDefinition(const std::string& defName) const;

		/// Generate or retrieve a cached tessellated mesh template for an asset.
		/// For instanced assets (grass), this returns the single template.
		/// For complex assets, this returns the default variant.
		/// @param defName The definition name
		/// @return Pointer to tessellated mesh, or nullptr if not found/failed
		const renderer::TessellatedMesh* getTemplate(const std::string& defName);

		/// Get the resolved motion (animation clips) for a def, or nullptr if it declares none.
		/// Driver pivots are resolved against the def's SVG nodes (find_node, by authored node
		/// index) into the mesh's scaled meter frame, so a part rotates about the right joint.
		/// Rotation amps are returned in radians, posX/posY amps in meters. Lazily loaded + cached.
		const MotionDef* getMotion(const std::string& defName);

		/// Generate an asset directly (does not cache)
		/// @param defName The definition name
		/// @param seed Random seed for generation
		/// @param outAsset Output generated asset
		/// @return true if generation succeeded
		bool generateAsset(const std::string& defName, uint32_t seed, GeneratedAsset& outAsset);

		/// Build an uncached tessellated mesh for an asset at a given seed.
		/// Simple assets ignore the seed (one drawing); procedural assets use it to
		/// select a form (vary the seed to sample the generator's range).
		/// This is the single shared "defName + seed -> mesh" entry point.
		/// @return true if a non-empty mesh was produced
		bool buildMesh(const std::string& defName, uint32_t seed, renderer::TessellatedMesh& outMesh);

		/// Parse a collision shape from an SVG file's <metadata><collision> block.
		/// Simple (SVG-backed) assets can author a collider directly in the SVG; the
		/// authored points are in SVG user units and are converted to the local-meter
		/// frame using the SAME scaleFactor (worldHeight / svgHeight) and centering
		/// (svg center -> origin) the render mesh uses in getTemplate +
		/// DynamicEntityRenderSystem, so the collider lands on the rendered art.
		///
		/// Returns nullopt (without loading the SVG) when the file carries no
		/// collision metadata, and nullopt + a warning when the authored shape is
		/// degenerate.
		/// @param absoluteSvgPath Absolute path to the .svg file
		/// @param worldHeight     The asset's worldHeight in meters (for normalization)
		[[nodiscard]] static std::optional<CollisionShape> loadCollisionFromSvgMetadata(
			const std::string& absoluteSvgPath, float worldHeight
		);

		/// Clear all loaded definitions and cached templates
		void clear();

		/// Get all loaded definition names
		std::vector<std::string> getDefinitionNames() const;

		/// Validation report from the most recent loadDefinitionsFromFolder.
		/// Produced at load time; shared by the game (launch) and the Asset Manager.
		[[nodiscard]] const ValidationReport& getValidationReport() const { return m_validationReport; }

		// --- Asynchronous loading (for a non-blocking splash) ---

		/// Load the asset folder on a background worker thread. Definitions are not
		/// safe to read until isLoadComplete() returns true. Only one async load may
		/// run at a time; a prior one is joined first.
		void beginLoadAsync(const std::string& folderPath);

		/// Progress of the most recent beginLoadAsync (atomics; safe to poll).
		[[nodiscard]] const LoadProgress& loadProgress() const { return m_loadProgress; }

		/// True once an async load has finished and definitions are safe to read.
		[[nodiscard]] bool isLoadComplete() const { return m_loadProgress.done.load(std::memory_order_acquire); }

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

		/// Mass of one unit of an item, in kilograms. Returns 1.0 for unknown/non-item defs
		/// so carry-weight math always has a sane positive divisor.
		[[nodiscard]] float getItemMassKg(const std::string& defName) const;

		/// Tool type an item provides (e.g. "Axe"), or empty if it is not a tool.
		[[nodiscard]] const std::string& getToolType(uint32_t id) const;

		/// Register a synthetic definition for terrain features (water tiles, etc.)
		/// These don't have XML definitions but need to participate in the capability system.
		/// @param defName The definition name (e.g., "Terrain_Water")
		/// @param capabilityMask Bitmask of capabilities
		/// @return The assigned defNameId, or 0 if registration failed
		uint32_t registerSyntheticDefinition(const std::string& defName, uint8_t capabilityMask);

		/// Get the total number of capability types
		static constexpr size_t kCapabilityTypeCount = 7;

		// --- Testing API ---

		/// Register an asset definition programmatically (for testing)
		/// @param def The asset definition to register
		void registerTestDefinition(AssetDefinition def);

		/// Clear all registered definitions (for testing cleanup)
		void clearDefinitions();

	  private:
		AssetRegistry() = default;
		~AssetRegistry();

		/// Tessellate a generated asset into a mesh
		bool tessellateAsset(const GeneratedAsset& asset, renderer::TessellatedMesh& outMesh);

		/// Build group index from loaded definitions
		void buildGroupIndex();

		/// Build string interning index from loaded definitions
		void buildDefNameIndex();

		std::unordered_map<std::string, AssetDefinition>		   definitions;
		std::unordered_map<std::string, renderer::TessellatedMesh> templateCache;

		// Resolved motion per def (empty MotionDef = "resolved, has none"). Guarded separately.
		std::unordered_map<std::string, MotionDef> m_motionCache;
		mutable std::mutex						   m_motionCacheMutex;

		// getTemplate lazily tessellates into templateCache and is called from
		// chunk worker threads (entity mesh baking) as well as the render thread
		mutable std::mutex templateCacheMutex;

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

		// Validation report from the most recent loadDefinitionsFromFolder
		ValidationReport m_validationReport;

		// Asynchronous load worker + its progress
		LoadProgress m_loadProgress;
		std::thread	 m_loadThread;
	};

} // namespace engine::assets
