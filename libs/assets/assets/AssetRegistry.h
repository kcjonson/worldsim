#pragma once

// Asset Registry
// Central catalog for asset definitions loaded from XML files.
// Handles definition loading, generator invocation, and template caching.

#include "assets/AssetDefinition.h"
#include "assets/AssetValidator.h"
#include "assets/IAssetGenerator.h"
#include "assets/MotionDef.h"
#include "assets/TessellatedPaths.h"
#include "assets/TreeTraits.h"

#include <polygon/Polygon.h>
#include <vector/Types.h>

#include <atomic>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace assets {

	/// Snapshot of asynchronous asset loading. All fields are atomic so the splash
	/// (main thread) can poll them while the load worker writes them.
	struct LoadProgress {
		std::atomic<bool> started{false};
		std::atomic<bool> done{false};
		std::atomic<int>  defsLoaded{0};
	};

	/// Cached per-defName selection silhouette: the filled outer outline of the
	/// asset's RENDERED footprint (rasterized from the tessellated mesh triangles, so
	/// it captures fills AND stroke bands — e.g. a reed's stroke stem — exactly what is
	/// drawn), in the template-local (scaled, uncentered) frame in integer millimeters.
	/// `rings` is the crisp outline (holes filled, disjoint blobs kept separate) used for
	/// the selection outline and later shadows; `hitRegion` is the same closed with a
	/// small morphological radius so gaps between disjoint blobs are clickable
	/// (whole-clump) and narrow concavities forgive slightly — used for hit-testing.
	/// `boundsCenterMeters` is the template mesh bbox centre in the same frame — it
	/// equals the dynamic render path's -centerOffset, stored so outline and hit-test
	/// share the render transform and can't drift.
	struct AssetSilhouette {
		std::vector<geometry::Ring> rings;
		std::vector<geometry::Ring> hitRegion;
		glm::vec2					boundsCenterMeters{0.0F, 0.0F};
		bool						valid = false;
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

		/// The template id of a def's mesh for an instance with `traits`, building the template the
		/// first time. One template per (def, age class, variant): a simple (SVG) asset is one
		/// drawing, and groundcover renders its variants itself, so both map every traits value to
		/// variant 0. The id is stable until the next clear and is what the renderers key their
		/// GPU meshes and instanced runs by. 0 when the def is unknown or the traits are out of
		/// its range (logged). A template whose generator fails keeps its id and has no mesh.
		/// Thread-safe; generation and tessellation run outside the cache lock.
		uint32_t templateId(const std::string& defName, TreeTraits traits);

		/// The mesh of a template id at its def's reference detail level (DetailLevels.h), or nullptr for 0
		/// or a template that failed to build. Everything that isn't drawing (silhouette, reach, routing,
		/// the cull margin) reads this mesh, so it doesn't change with the camera's zoom.
		const renderer::TessellatedMesh* getTemplate(uint32_t templateId) const;

		/// A template's mesh at one detail level, or why there is none, and the collision rect its
		/// generator emitted (asset:setCollisionRect).
		struct BuiltTemplate {
			std::optional<renderer::TessellatedMesh> mesh;
			std::string								 error;
			std::optional<CollisionShape>			 collision;
		};

		/// The <detailLevels> of a template's def: the densities its levels are cut for, ascending. Empty
		/// for 0 or a def without levels, whose one mesh is level 0. Valid until the next clear.
		[[nodiscard]] std::span<const float> detailLevels(uint32_t templateId) const;

		/// Builds a template's mesh at one of its def's detail levels (0 for a def without levels),
		/// uncached: getTemplate keeps the reference level's, the renderer keeps the others it draws.
		/// Touches no cache, so it runs on any thread while no load or clear runs.
		[[nodiscard]] BuiltTemplate buildTemplateLevel(uint32_t templateId, uint8_t level);

		/// getTemplate(templateId(defName, traits)).
		const renderer::TessellatedMesh* getTemplate(const std::string& defName, TreeTraits traits);

		/// The seed a template is generated with: 0 for a def with a single variant, else the
		/// shared variantSeed hash of the def and age class name, at the variant.
		[[nodiscard]] static uint32_t generationSeed(const AssetDefinition& def, TreeTraits traits);

		/// The traits of a def's mature variant 0: the age class named "mature", or the first
		/// when it has none of that name.
		[[nodiscard]] static TreeTraits matureTraits(const AssetDefinition& def);

		/// Whether every instance of the def draws in the Y-sorted upright stream (routesTall on
		/// its mature variant 0's height). Builds that template the first time.
		bool isTallDef(const std::string& defName);

		/// The farthest any drawn point of a template but groundcover reaches from its origin,
		/// times the def's largest placement scale, over every template built so far: how far
		/// from its origin an entity can draw or be clicked. Every placed static has its
		/// template built at placement, so this covers what can be on screen.
		[[nodiscard]] float maxReachMeters() const { return m_maxReach.load(std::memory_order_relaxed); }

		/// Get the resolved motion (animation clips) for a def, or nullptr if it declares none.
		/// Driver pivots are resolved against the def's SVG nodes (find_node, by authored node
		/// index) into the mesh's scaled meter frame, so a part rotates about the right joint.
		/// Rotation amps are returned in radians, posX/posY amps in meters. Lazily loaded + cached.
		const MotionDef* getMotion(const std::string& defName);

		/// Get the cached selection silhouette for a template (see AssetSilhouette).
		/// Lazily rasterized from the tessellated template mesh triangles (fills AND stroke
		/// bands) via geometry::silhouetteOfTriangles: `rings` at close 0, `hitRegion` at a
		/// small close; holes filled, disjoint blobs kept separate. Triangles under
		/// kSilhouetteMinAlpha (shadows) are left out. `valid` is false only when nothing
		/// opaque is drawn (no bounds-rect fallback). Thread-safe (own mutex, heavy compute
		/// off-lock), safe to call from chunk workers like getTemplate.
		const AssetSilhouette* getSilhouette(uint32_t templateId);

		/// getSilhouette(templateId(defName, traits)).
		const AssetSilhouette* getSilhouette(const std::string& defName, TreeTraits traits);

		/// A triangle whose vertices are all below this alpha (a cast shadow) is not part of
		/// the silhouette, so it is neither outlined, clicked, nor counted as canopy.
		static constexpr float kSilhouetteMinAlpha = 0.5F;

		/// Generate an asset directly (does not cache)
		/// @param defName The definition name
		/// @param seed Random seed for generation; Lua seeds math.random from it alone
		/// @param outAsset Output generated asset
		/// @param traits The variant being generated: Lua sees variantIndex and the age class
		///        name as ageClass (nil for a def without age classes)
		/// @return true if generation succeeded
		/// @param error When given, receives the script error of a failed Lua generation
		/// @param level The def's detail level to cut for (Lua sees its density as detailPxPerMeter, nil
		///        for a def without levels); the reference level when left out
		bool generateAsset(
			const std::string&	   defName,
			uint32_t			   seed,
			GeneratedAsset&		   outAsset,
			TreeTraits			   traits = {},
			std::string*		   error = nullptr,
			std::optional<uint8_t> level = std::nullopt
		);

		/// A pixel density that keeps every path, whatever its setLod threshold.
		static constexpr float kFullDetail = std::numeric_limits<float>::infinity();

		/// Build an uncached tessellated mesh for an asset at a given seed.
		/// Simple assets ignore the seed (one drawing) but for an SVG ground def, where it picks the
		/// variant; procedural assets use it to select a form (vary the seed to sample the generator's range).
		/// This is the single shared "defName + seed -> mesh" entry point.
		/// For callers that draw one mesh. An asset over the 65535 vertices a mesh can index
		/// needs several, and that fails here (logged); use buildMeshes for those.
		/// @param pixelsPerMeter As buildMeshes'.
		/// @return true if a non-empty mesh was produced
		bool buildMesh(const std::string& defName, uint32_t seed, renderer::TessellatedMesh& outMesh, float pixelsPerMeter = kFullDetail);

		/// buildMesh for assets of any size: the tessellated paths are packed into as many
		/// meshes as it takes to keep each under 65535 vertices (16-bit indices), in path order.
		/// A single path that alone tessellates past that is dropped with a warning.
		/// A ground tile comes back wrapped: a path crossing an edge of its [0, period] square is
		/// repeated where it crosses into, so the meshes clipped to that square are the seamless tile.
		/// @param pixelsPerMeter Screen density to build for. A path that called setLod(n), or a
		///        ground SVG's shape under data-lod="n", is kept only when n <= this; the default
		///        keeps all of them. A def with <detailLevels> is cut for the level this density
		///        draws (detailLevelFor), its reference level by default. Other simple assets are
		///        always one drawing, whatever the density.
		/// @return true if at least one non-empty mesh was produced
		bool buildMeshes(
			const std::string& defName, uint32_t seed, std::vector<renderer::TessellatedMesh>& outMeshes, float pixelsPerMeter = kFullDetail
		);

		/// The path half of buildMeshes, for callers that want several densities from one generator
		/// run or SVG read: the seed's paths, each tessellated once (a ground tile's wrapped, as
		/// above), so outPaths.meshesAt(d) is what buildMeshes returns at d. An SVG ground def's seed
		/// picks its variant's SVG, seed modulo their count (GroundSvg.h reads it). Safe to call from
		/// several threads at once while no load or clear runs: each call runs its own generator
		/// (a fresh Lua state for a script) or reads its own SVG, and touches no cache.
		/// @param maxPixelsPerMeter The finest density meshesAt will be asked for; paths that need
		///        more aren't tessellated.
		/// @return false when the def is neither procedural nor a ground tile, its generator fails, or
		///         its SVG breaks a ground SVG rule (logged)
		bool buildPaths(const std::string& defName, uint32_t seed, TessellatedPaths& outPaths, float maxPixelsPerMeter = kFullDetail);

		/// Adds an error to `report` for each age class of a procedural def whose generator emits a
		/// different collision rect for two of its variants: the rect is per age class, so it may not
		/// depend on the variant. A def without <ageClasses> is one implicit class 0 of variantCount
		/// variants. Generates variants 0 and 1 of each class that has two or more; runs a
		/// generator, so it is for the render smoke, not for load.
		void checkCollisionVariantIndependence(ValidationReport& report);

		/// Builds every template of each def with age classes and <detailLevels> at every level, in
		/// parallel, and adds an error to `report` for a level whose generator fails or whose collision
		/// rect differs from the reference level's. For the render smoke, not for load.
		void checkDetailLevels(ValidationReport& report);

		/// Writes `params` over a def's generator params, as if its XML said so, and drops every built
		/// template, silhouette and motion so nothing built from the old params is drawn (template ids
		/// start over). For a tool run that draws one def with changed params; the def's collision rects
		/// stay as loaded.
		void overrideGeneratorParams(const std::string& defName, const std::vector<std::pair<std::string, std::string>>& params);

		/// Where a def's generator script is on disk: its scriptPath, with @shared/ resolved against
		/// the shared scripts folder. Empty when the def names no script, or a @shared/ one with no
		/// folder set (logged).
		[[nodiscard]] std::filesystem::path resolveScriptPath(const AssetDefinition& def) const;

		/// The folder of Lua files that run before every generator: lib/ in the shared scripts folder.
		/// Empty when no shared scripts folder is set.
		[[nodiscard]] std::filesystem::path preludeDir() const;

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
		/// Returns a bitmask where bit N is set if capability N is present. 16-bit because there are
		/// 9 capability types (Edible..Storage); Storage is bit 8, which overflows a uint8_t.
		/// @param id The defName ID
		/// @return Capability bitmask (0 if not found)
		[[nodiscard]] uint16_t getCapabilityMask(uint32_t id) const;

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
		uint32_t registerSyntheticDefinition(const std::string& defName, uint16_t capabilityMask);

		/// Get the total number of capability types (Edible..Storage)
		static constexpr size_t kCapabilityTypeCount = 9;

		// --- Testing API ---

		/// Register an asset definition programmatically (for testing)
		/// @param def The asset definition to register
		void registerTestDefinition(AssetDefinition def);

		/// Clear all registered definitions (for testing cleanup)
		void clearDefinitions();

	  private:
		AssetRegistry() = default;
		~AssetRegistry();

		/// Build group index from loaded definitions
		void buildGroupIndex();

		/// Build string interning index from loaded definitions
		void buildDefNameIndex();

		struct TemplateKey {
			std::string defName;
			TreeTraits	traits;

			bool operator==(const TemplateKey&) const = default;
		};
		struct TemplateKeyHash {
			size_t operator()(const TemplateKey& key) const;
		};

		/// A template: its key's def and traits, and its reference level's mesh. The level is not part of
		/// the key; a template has a mesh at every level of its def, built on request (buildTemplateLevel).
		struct TemplateEntry {
			const AssetDefinition*					 def = nullptr;
			TreeTraits								 traits;
			std::optional<renderer::TessellatedMesh> mesh;
			bool									 defTall = false;
		};

		/// The traits a template of `def` is keyed by (see templateId), or nullopt when out of range.
		[[nodiscard]] static std::optional<TreeTraits> templateTraits(const AssetDefinition& def, TreeTraits traits);

		/// Generates and tessellates one template at one detail level. Touches no cache, so it runs on any thread.
		[[nodiscard]] BuiltTemplate buildTemplate(const AssetDefinition& def, TreeTraits traits, uint8_t level);

		/// Files a built template under its key and returns its id; when another thread filed the
		/// key first, that one's id. Resolves the def's routing first, outside the lock.
		uint32_t insertTemplate(const AssetDefinition& def, TreeTraits traits, BuiltTemplate built);

		/// Parses one XML file into `definitions`, appending the names it defined to `loadedNames`.
		/// Runs the per-def collision passes that need no age classes; leaves each aged def's
		/// templates and rects to prewarmTreeVariants.
		bool parseDefinitions(const std::string& xmlPath, std::vector<std::string>& loadedNames);

		/// Builds every variant of each named def with age classes at its reference detail level (the
		/// default view's; the renderer requests the others as the camera needs them), in parallel,
		/// before the first chunk asks for one. A variant over kMaxMeshVertices is an error in the validation report.
		/// Each class's collision rect is taken from its variant 0 (unless the def authors one).
		/// Both load entry points end here, so a def is complete however it was loaded.
		void prewarmTreeVariants(const std::vector<std::string>& defNames);

		/// Drops every template, silhouette, and resolved motion.
		void clearCaches();

		std::unordered_map<std::string, AssetDefinition> definitions;

		// Templates by id - 1. unique_ptr keeps each entry's address stable while the vector grows,
		// so getTemplate's pointer outlives the lock. Guarded by templateCacheMutex, which getTemplate
		// holds only to look up or insert: chunk workers and the render thread both ask.
		std::unordered_map<TemplateKey, uint32_t, TemplateKeyHash> m_templateIds;
		std::vector<std::unique_ptr<TemplateEntry>>				m_templates;
		mutable std::mutex										templateCacheMutex;
		std::atomic<float>										m_maxReach{0.0F};

		// Resolved motion per def (empty MotionDef = "resolved, has none"). Guarded separately.
		std::unordered_map<std::string, MotionDef> m_motionCache;
		mutable std::mutex						   m_motionCacheMutex;

		// Lazily-computed selection silhouette per template id. Guarded separately; the raster
		// rasterize/flood-fill runs off-lock (like getMotion), the cache lock is held only
		// to check/insert. Rasterized from the template mesh triangles.
		std::unordered_map<uint32_t, AssetSilhouette> m_silhouetteCache;
		mutable std::mutex							  m_silhouetteCacheMutex;

		// Group index: group name → list of defNames that belong to it
		std::unordered_map<std::string, std::vector<std::string>> groupIndex;

		// String interning: defName ↔ ID mapping for memory-efficient storage
		// ID 0 is reserved as "invalid/not found"
		std::unordered_map<std::string, uint32_t> m_defNameToId;
		std::vector<std::string>				  m_idToDefName; // Index 0 = empty string (invalid)

		// Pre-computed capability masks by ID (for O(1) capability checks). 16-bit: 9 capability types.
		std::vector<uint16_t> m_capabilityMasks;

		// Path to shared scripts folder (for @shared/ prefix resolution)
		std::filesystem::path m_sharedScriptsPath;

		// Validation report from the most recent loadDefinitionsFromFolder
		ValidationReport m_validationReport;

		// Asynchronous load worker + its progress
		LoadProgress m_loadProgress;
		std::thread	 m_loadThread;
	};

} // namespace assets
