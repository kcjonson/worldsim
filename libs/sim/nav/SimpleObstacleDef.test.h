#pragma once

// A simple (SVG) obstacle def for tests of where a placed entity's collider stands.

#include <assets/AssetRegistry.h>

#include <glm/vec2.hpp>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

namespace nav::testing {

	inline constexpr const char* kSimpleObstacleDefName = "Test_SimpleObstacle";
	// The drawing is 3 m by 1 m from its origin, so its bbox centre, which the collider is authored
	// about, is this far from where a static's position puts the mesh's origin.
	inline constexpr glm::vec2 kSimpleObstacleFrameOrigin{1.5F, 0.5F};
	// The collider: a square at the drawing's centre.
	inline constexpr float kSimpleObstacleHalfExtent = 0.1F;

	// Loads the def into `registry` from a temporary folder and returns its template id.
	inline std::uint32_t loadSimpleObstacleDef(assets::AssetRegistry& registry) {
		static std::atomic<int>		counter{0};
		const std::filesystem::path root =
			std::filesystem::temp_directory_path() / ("simple_obstacle_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(&counter)) + "_" +
													  std::to_string(counter++));
		const std::filesystem::path folder = root / kSimpleObstacleDefName;
		std::filesystem::create_directories(folder);
		std::ofstream(folder / "shape.svg")
			<< "<?xml version=\"1.0\"?>"
			   "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 300 100\" width=\"300\" height=\"100\">"
			   "<metadata><collision><shape type=\"aabb\" min=\"140,40\" max=\"160,60\"/></collision></metadata>"
			   "<path d=\"M0,0 L300,0 L300,100 L0,100 Z\" fill=\"#777777\"/>"
			   "</svg>";
		std::ofstream(folder / (std::string(kSimpleObstacleDefName) + ".xml"))
			<< "<?xml version=\"1.0\"?><AssetDefinitions><AssetDef>"
			   "<defName>" << kSimpleObstacleDefName << "</defName>"
			   "<assetType>simple</assetType>"
			   "<svgPath>shape.svg</svgPath>"
			   "<worldHeight>1.0</worldHeight>"
			   "</AssetDef></AssetDefinitions>";
		registry.loadDefinitionsFromFolder(root.string());
		const std::uint32_t templateId = registry.templateId(kSimpleObstacleDefName, {});
		std::error_code		ignored;
		std::filesystem::remove_all(root, ignored);
		return templateId;
	}

} // namespace nav::testing
