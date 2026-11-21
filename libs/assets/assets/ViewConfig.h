#pragma once

// How things with height are drawn: assets/config/rendering/view.xml (docs/technical/rendering/
// rotatable-placed-items.md, sections 2, 6, 7, and 18). Every length is in metres.

#include <graphics/Color.h>

#include <filesystem>
#include <string>
#include <vector>

namespace assets {

	/// Where the view config is, under the config root (assets/config).
	inline constexpr const char* kViewConfigFile = "rendering/view.xml";

	struct ViewConfig {
		/// Screen metres a metre of height rises: screenY = y - z * heightRatio.
		float heightRatio = 0.0F;

		/// The one light, degrees clockwise from the top of the screen and above the ground.
		float lightAzimuthDegrees = 0.0F;
		float lightElevationDegrees = 0.0F;

		/// Side shading (section 7).
		float ambient = 0.0F;
		float diffuse = 0.0F;
		float wrap = 0.0F;

		/// Part outlines (section 6.5).
		Foundation::Color outlineColor;
		float			  outlineWidthMeters = 0.0F;
		Foundation::Color outlineTopColor; ///< its alpha is topOpacity
		float			  outlineTopWidthMeters = 0.0F;

		/// Item drawings (sections 5 and 6).
		float aspectToleranceRelative = 0.0F;
		float curveToleranceMeters = 0.0F;
		float overflowMeters = 0.0F;
		float faceDetailMinWidthMeters = 0.0F;

		/// Depth strips (section 6.7).
		float depthStripWidthMeters = 0.0F;
		float depthStripRiseMeters = 0.0F;
		float depthStripMergeMeters = 0.0F;

		/// Half the wider of the two outline strokes: how far a part's drawing reaches past its footprint.
		[[nodiscard]] float outlineHalfWidthMeters() const;
	};

	/// The game's view config, read once with the asset system (loadAssetSystem) and kept until the next load.
	class GameViewConfig {
	  public:
		static GameViewConfig& Get();

		/// Replaces the config with `configRoot / file`: false, with problems() saying why, when the file can't be
		/// read or a value in it is missing, malformed, or out of range. Errors name the file as `file` is written.
		bool loadFromFile(const std::filesystem::path& configRoot, const std::filesystem::path& file);

		/// Forgets the config, with `problem` as the reason when it is not empty.
		void clear(const std::string& problem = {});

		[[nodiscard]] bool loaded() const { return isLoaded; }

		/// Only meaningful while loaded().
		[[nodiscard]] const ViewConfig& config() const { return values; }

		/// What the file got wrong: each names the file, the element and the attribute.
		[[nodiscard]] const std::vector<std::string>& problems() const { return problemList; }

	  private:
		bool					 isLoaded = false;
		ViewConfig				 values;
		std::vector<std::string> problemList;
	};

} // namespace assets
