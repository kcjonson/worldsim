#include "SightHeight.h"

#include <assets/AssetDefinition.h>

#include <cmath>

namespace vision {

	std::optional<std::int64_t> bodyTopMm(const assets::AssetDefinition& def) {
		switch (def.assetType) {
			case assets::AssetType::Simple:
				return std::llround(static_cast<double>(def.worldHeight) * 1000.0);
			case assets::AssetType::Procedural:
				return std::nullopt;
		}
		return std::nullopt;
	}

	bool reachesEyeHeight(const assets::AssetDefinition& def) {
		const std::optional<std::int64_t> top = bodyTopMm(def);
		return !top.has_value() || *top >= kEyeHeightMm;
	}

} // namespace vision
