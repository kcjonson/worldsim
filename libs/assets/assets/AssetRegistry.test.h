#pragma once

// Test helpers that wait for AssetRegistry::beginLoadAsync to finish.
//
// There is no deadline. The load runs every Lua generator in the shipped library,
// which a Debug build on a slow CI runner can stretch well past the old 10 s budget,
// and that budget couldn't tell a slow runner from a hang. The worker always ends by
// setting done (a throw terminates the process), so a load that never finishes is a
// hang, caught by the TIMEOUT on the assets-tests ctest entry. Every kHeartbeat the
// wait prints how many definitions have loaded, so a timed-out log shows whether the
// load was still advancing.

#include "assets/AssetRegistry.h"
#include "assets/ViewConfig.h"

#include <polygon/Polygon.h>
#include <predicates/Predicates.h>
#include <silhouette/Silhouette.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

namespace assets {

	/// How far a point lies outside every ring, in millimetres; 0 in or on one.
	inline double millimetresOutside(const std::vector<geometry::Ring>& rings, const geometry::Vec2i64& point) {
		double nearest = 1.0e18;
		for (const geometry::Ring& ring : rings) {
			if (geometry::pointInPolygon(point, ring) != geometry::PointInPolygon::Outside) {
				return 0.0;
			}
			for (size_t i = 0; i < ring.size(); ++i) {
				const geometry::Vec2i64 from = ring[i];
				const geometry::Vec2i64 to = ring[(i + 1) % ring.size()];
				const double alongX = static_cast<double>(to.x - from.x);
				const double alongY = static_cast<double>(to.y - from.y);
				const double lengthSquared = alongX * alongX + alongY * alongY;
				const double fraction =
					lengthSquared > 0.0 ? std::clamp((static_cast<double>(point.x - from.x) * alongX + static_cast<double>(point.y - from.y) * alongY) / lengthSquared, 0.0, 1.0)
										: 0.0;
				nearest = std::min(nearest, std::hypot(static_cast<double>(point.x - from.x) - fraction * alongX, static_cast<double>(point.y - from.y) - fraction * alongY));
			}
		}
		return nearest;
	}

	/// Whether a point lies in or on some ring, or within a millimetre of one (the booleans simplify by 1 mm).
	inline bool withinMillimetreOf(const std::vector<geometry::Ring>& rings, const geometry::Vec2i64& point) {
		return millimetresOutside(rings, point) <= 1.0;
	}

	/// The opaque vertices of a mesh in millimetres (those of triangles not wholly under kSilhouetteMinAlpha), and the
	/// count of those triangles.
	inline std::vector<geometry::Vec2i64> opaqueVertices(const renderer::TessellatedMesh& mesh, size_t& triangleCount) {
		std::vector<geometry::Vec2i64> vertices;
		triangleCount = 0;
		const auto faint = [&](uint16_t index) { return mesh.hasColors() && mesh.colors[index].a < kSilhouetteMinAlpha; };
		for (size_t triangle = 0; triangle + 2 < mesh.indices.size(); triangle += 3) {
			const uint16_t first = mesh.indices[triangle];
			const uint16_t second = mesh.indices[triangle + 1];
			const uint16_t third = mesh.indices[triangle + 2];
			if (faint(first) && faint(second) && faint(third)) {
				continue;
			}
			++triangleCount;
			for (const uint16_t index : {first, second, third}) {
				vertices.push_back(geometry::quantize(mesh.vertices[index]));
			}
		}
		return vertices;
	}

	/// The longest distance of a mesh's opaque vertices outside its silhouette's rings, and the bound it is held to
	/// (geometry::silhouetteBoundMm at the silhouette's tolerance).
	struct OutlineReach {
		double		 worstMm = 0.0;
		std::int64_t boundMm = 0;
	};
	inline OutlineReach outlineReach(const renderer::TessellatedMesh& mesh, const AssetSilhouette& silhouette) {
		size_t									 triangleCount = 0;
		const std::vector<geometry::Vec2i64> vertices = opaqueVertices(mesh, triangleCount);
		geometry::Vec2i64						 low = vertices.front();
		geometry::Vec2i64						 high = low;
		OutlineReach							 reach;
		for (const geometry::Vec2i64& vertex : vertices) {
			low = {std::min(low.x, vertex.x), std::min(low.y, vertex.y)};
			high = {std::max(high.x, vertex.x), std::max(high.y, vertex.y)};
			reach.worstMm = std::max(reach.worstMm, millimetresOutside(silhouette.rings, vertex));
		}
		const std::int64_t tolerance = std::max<std::int64_t>(1, std::max(high.x - low.x, high.y - low.y) / kSilhouetteToleranceDivisor);
		reach.boundMm = geometry::silhouetteBoundMm(triangleCount, tolerance);
		return reach;
	}

	inline void waitForAsyncLoad(const AssetRegistry& reg) {
		using Clock = std::chrono::steady_clock;
		constexpr auto kHeartbeat = std::chrono::seconds(30);

		const auto start = Clock::now();
		auto	   nextHeartbeat = start + kHeartbeat;
		while (!reg.isLoadComplete()) {
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
			const auto now = Clock::now();
			if (now >= nextHeartbeat) {
				const auto secs = std::chrono::duration_cast<std::chrono::seconds>(now - start).count();
				printf(
					"[AssetRegistry] still loading after %lld s: %d definitions\n",
					static_cast<long long>(secs),
					reg.loadProgress().defsLoaded.load()
				);
				fflush(stdout);
				nextHeartbeat += kHeartbeat;
			}
		}
	}

	// A throwaway asset folder in the temp directory, removed with it. Each def goes in
	// <Name>/<Name>.xml, the layout loadDefinitionsFromFolder reads.
	class TempAssetFolder {
	  public:
		TempAssetFolder() {
			static std::atomic<int> counter{0};
			root = std::filesystem::temp_directory_path() /
				   ("asset_folder_" + std::to_string(reinterpret_cast<uintptr_t>(&counter)) + "_" + std::to_string(counter++));
			std::filesystem::create_directories(root);
		}
		~TempAssetFolder() {
			std::error_code ec;
			std::filesystem::remove_all(root, ec);
		}
		TempAssetFolder(const TempAssetFolder&) = delete;
		TempAssetFolder& operator=(const TempAssetFolder&) = delete;

		void write(const std::string& relPath, const std::string& content) const {
			const std::filesystem::path p = root / relPath;
			std::filesystem::create_directories(p.parent_path());
			std::ofstream(p) << content;
		}

		std::filesystem::path root;
	};

	// The shipped config root, <root>/assets/config.
	inline std::filesystem::path shippedConfigFolder() {
		// <root>/libs/assets/assets/AssetRegistry.test.h -> walk up to <root>.
		return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path() / "assets" / "config";
	}

	// Loads the shipped view config into GameViewConfig, which parts defs are read with; false when it doesn't load.
	inline bool loadShippedViewConfig() {
		return GameViewConfig::Get().loadFromFile(shippedConfigFolder(), kViewConfigFile);
	}

	// The view config the tests' numbers are worked against, owned here so tuning the shipped file breaks no test.
	inline constexpr const char* kTestViewXml = R"(<?xml version="1.0"?>
<WorldView>
  <heightRatio>0.35</heightRatio>
  <light azimuthDegrees="315" elevationDegrees="40"/>
  <sideShading ambient="0.66" diffuse="0.6" wrap="0"/>
  <partOutline color="#33241A" widthMeters="0.03" topColor="#33241A" topWidthMeters="0.025" topOpacity="0.8"/>
  <drawings aspectTolerance="0.01" curveToleranceMeters="0.002" overflowMeters="0.01" faceDetailMinWidthMeters="0.04"/>
  <depthStripWidthMeters>0.25</depthStripWidthMeters>
  <depthStripRiseMeters>0.1</depthStripRiseMeters>
  <depthStripMergeMeters>0.02</depthStripMergeMeters>
</WorldView>
)";

	// A config root holding kTestViewXml as rendering/view.xml, written once and removed at exit.
	inline const std::filesystem::path& testConfigFolder() {
		static const TempAssetFolder folder;
		static const bool			 written = (folder.write(kViewConfigFile, kTestViewXml), true);
		(void)written;
		return folder.root;
	}

	// Loads kTestViewXml into GameViewConfig; false when it doesn't load.
	inline bool loadTestViewConfig() {
		return GameViewConfig::Get().loadFromFile(testConfigFolder(), kViewConfigFile);
	}

	// Loads the shipped library (<root>/assets/world, with its shared scripts) through
	// beginLoadAsync and waits for it. False when the library isn't there to load.
	inline bool loadShippedLibrary(AssetRegistry& reg) {
		// <root>/libs/assets/assets/AssetRegistry.test.h -> walk up to <root>.
		const std::filesystem::path root = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
		const std::filesystem::path world = root / "assets" / "world";
		if (!std::filesystem::exists(world)) {
			return false;
		}
		reg.setSharedScriptsPath(root / "assets" / "shared" / "scripts");
		loadShippedViewConfig();
		reg.beginLoadAsync(world.string());
		waitForAsyncLoad(reg);
		return true;
	}

} // namespace assets
