#include "PartsPreview.h"

#include <CoordinateSystem/CoordinateSystem.h>
#include <assets/AssetRegistry.h>
#include <assets/DetailLevels.h>
#include <assets/ViewConfig.h>
#include <ui/Context.h>
#include <utils/Log.h>

#include <algorithm>
#include <cmath>
#include <span>

namespace asset_manager {

	namespace {
		// Room round the farthest thing drawn, so nothing touches the box's edge.
		constexpr float kFitMargin = 1.06F;
	} // namespace

	float partsFitRadiusMeters(const std::string& defName) {
		const assets::AssetDefinition* def = assets::AssetRegistry::Get().getDefinition(defName);
		if (def == nullptr || def->parts == nullptr) {
			return 1.0F;
		}
		float radius = assets::itemReachMeters(*def->parts, assets::GameViewConfig::Get().config());
		const auto reach = [&radius](const std::vector<assets::AreaPolygon>& polygons) {
			for (const assets::AreaPolygon& polygon : polygons) {
				for (const glm::vec2& point : polygon.outer) {
					radius = std::max(radius, std::hypot(point.x, point.y));
				}
			}
		};
		for (const assets::ItemArea& area : def->parts->useAreas) {
			reach(area.polygons);
		}
		reach(def->parts->clearance.polygons);
		return std::max(radius, 0.01F) * kFitMargin;
	}

	PartsPreview::PartsPreview(const Args& args)
		: defName(args.defName),
		  turnQuanta(args.turnQuanta),
		  overlays(args.overlays),
		  fixedLevel(args.level),
		  labels(args.labels),
		  coordinateSystem(args.coordinateSystem) {}

	void PartsPreview::setTurn(uint32_t value) {
		turnQuanta = value;
	}

	void PartsPreview::setOverlays(const PartsOverlays& value) {
		overlays = value;
	}

	void PartsPreview::setLevel(std::optional<uint8_t> value) {
		fixedLevel = value;
	}

	bool PartsPreview::refresh(uint8_t level) {
		// Re-resolved by name: a reload clears the registry, and a stored definition would dangle.
		const assets::AssetRegistry&   registry = assets::AssetRegistry::Get();
		const assets::AssetDefinition* def = registry.getDefinition(defName);
		if (def == nullptr || def->parts == nullptr || !assets::GameViewConfig::Get().loaded()) {
			hasView = false;
			return false;
		}
		const uint64_t generation = registry.generation();
		if (hasView && builtGeneration == generation && built.turnQuanta == turnQuanta && built.level == level) {
			return true;
		}
		if (!hasView || builtGeneration != generation) {
			fitRadiusMeters = partsFitRadiusMeters(defName);
		}
		if (!hasView || builtGeneration != generation || geometry.turnQuanta != turnQuanta) {
			geometry = assets::itemOverlays(*def, turnQuanta);
		}
		built = assets::buildItemMesh(*def, turnQuanta, level, assets::GameViewConfig::Get().config());
		if (!built.error.empty()) {
			LOG_ERROR(Engine, "PartsPreview: no view of %s at %u quanta: %s", defName.c_str(), turnQuanta, built.error.c_str());
		}
		builtGeneration = generation;
		hasView = true;
		return true;
	}

	void PartsPreview::render() {
		const Foundation::Vec2 size = getSize();
		if (size.x <= 0.0F || size.y <= 0.0F) {
			return;
		}
		const assets::AssetDefinition* def = assets::AssetRegistry::Get().getDefinition(defName);
		if (def == nullptr || def->parts == nullptr) {
			return;
		}
		// Fit before the build: the level is the one the fitted density draws.
		const float pixelsPerMeter = std::min(size.x, size.y) / (2.0F * partsFitRadiusMeters(defName));
		const float ratio = coordinateSystem != nullptr ? coordinateSystem->getPixelRatio() : 1.0F;
		const float devicePixelsPerMeter = pixelsPerMeter * ratio * (getContext() != nullptr ? getContext()->getScale() : 1.0F);
		const uint8_t level = fixedLevel.has_value() && *fixedLevel < std::max<size_t>(def->detailLevels.size(), 1)
								  ? *fixedLevel
								  : assets::detailLevelFor(std::span<const float>(def->detailLevels), devicePixelsPerMeter);
		if (!refresh(level)) {
			return;
		}

		const worldview::ItemScreenFrame frame{.pivot = {size.x * 0.5F, size.y * 0.5F}, .pixelsPerMeter = pixelsPerMeter};
		const worldview::ItemOverlayStyle style{.opacity = opacity(), .labels = labels, .key = drawKey()};
		worldview::drawItemOverlays(
			geometry, frame, {.useAreas = overlays.useAreas, .clearance = overlays.clearance, .footprints = false, .collider = false}, style
		);
		worldview::drawItemView(built.mesh, frame, drawKey());
		worldview::drawItemOverlays(
			geometry, frame, {.useAreas = false, .clearance = false, .footprints = overlays.footprints, .collider = overlays.collider}, style
		);
		if (overlays.strips) {
			worldview::drawItemStrips(built, frame, fitRadiusMeters, style);
		}
	}

} // namespace asset_manager
