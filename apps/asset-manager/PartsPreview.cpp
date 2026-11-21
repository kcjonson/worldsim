#include "PartsPreview.h"

#include <assets/AssetRegistry.h>
#include <assets/ConstructionRegistry.h>
#include <assets/ViewConfig.h>
#include <utils/Log.h>

#include <algorithm>
#include <cmath>

namespace asset_manager {

	namespace {
		// Room round the farthest thing drawn, so nothing touches the box's edge.
		constexpr float kFitMargin = 1.06F;

		const assets::ItemOverlayStyle& look() {
			return assets::ConstructionRegistry::Get().rendering().itemOverlays;
		}
	} // namespace

	float partsFitRadiusMeters(const assets::AssetDefinition& def) {
		if (def.parts == nullptr) {
			return 1.0F;
		}
		float radius = assets::itemReachMeters(*def.parts, assets::GameViewConfig::Get().config());
		const auto reach = [&radius](const std::vector<assets::AreaPolygon>& polygons) {
			for (const assets::AreaPolygon& polygon : polygons) {
				for (const glm::vec2& point : polygon.outer) {
					radius = std::max(radius, std::hypot(point.x, point.y));
				}
			}
		};
		for (const assets::ItemArea& area : def.parts->useAreas) {
			reach(area.polygons);
		}
		reach(def.parts->clearance.polygons);
		return std::max(radius, 0.01F) * kFitMargin;
	}

	uint8_t partsPreviewLevel(const assets::AssetDefinition& def, std::optional<uint8_t> level) {
		const uint8_t finest = static_cast<uint8_t>(std::max<size_t>(def.detailLevels.size(), 1) - 1);
		return level.has_value() && *level <= finest ? *level : finest;
	}

	PartsPreview::PartsPreview(const Args& args)
		: defName(args.defName),
		  turnQuanta(args.turnQuanta),
		  overlays(args.overlays),
		  fixedLevel(args.level),
		  labels(args.labels) {}

	void PartsPreview::setTurn(uint32_t value) {
		turnQuanta = value;
	}

	void PartsPreview::setOverlays(const PartsOverlays& value) {
		overlays = value;
	}

	const assets::AssetDefinition* PartsPreview::refresh() {
		// Re-resolved by name: a reload clears the registry, and a stored definition would dangle.
		const assets::AssetRegistry&   registry = assets::AssetRegistry::Get();
		const assets::AssetDefinition* def = registry.getDefinition(defName);
		if (def == nullptr || def->parts == nullptr || !assets::GameViewConfig::Get().loaded()) {
			hasView = false;
			return nullptr;
		}
		const uint64_t generation = registry.generation();
		const uint8_t  level = partsPreviewLevel(*def, fixedLevel);
		const bool	   fresh = hasView && builtGeneration == generation;
		if (fresh && built.turnQuanta == turnQuanta && built.level == level) {
			return def;
		}
		if (!fresh) {
			fitRadiusMeters = partsFitRadiusMeters(*def);
		}
		if (!fresh || geometry.turnQuanta != turnQuanta) {
			geometry = assets::itemOverlays(*def, turnQuanta, look().hatchSpacingMeters);
		}
		built = assets::buildItemMesh(*def, turnQuanta, level, assets::GameViewConfig::Get().config());
		if (!built.error.empty()) {
			LOG_ERROR(Engine, "PartsPreview: no view of %s at %u quanta: %s", defName.c_str(), turnQuanta, built.error.c_str());
		}
		builtGeneration = generation;
		hasView = true;
		return def;
	}

	void PartsPreview::render() {
		const Foundation::Vec2 size = getSize();
		if (size.x <= 0.0F || size.y <= 0.0F || refresh() == nullptr) {
			return;
		}
		const worldview::ItemScreenFrame frame{
			.pivot = {size.x * 0.5F, size.y * 0.5F}, .pixelsPerMeter = std::min(size.x, size.y) / (2.0F * fitRadiusMeters)
		};
		const worldview::ItemDrawStyle style{.opacity = opacity(), .labels = labels, .key = drawKey()};
		worldview::drawItemOverlays(
			geometry, frame, {.useAreas = overlays.useAreas, .clearance = overlays.clearance, .footprints = false, .collider = false}, look(), style
		);
		worldview::drawItemView(built.mesh, frame, look(), style);
		worldview::drawItemOverlays(
			geometry, frame, {.useAreas = false, .clearance = false, .footprints = overlays.footprints, .collider = overlays.collider}, look(), style
		);
		if (overlays.strips) {
			worldview::drawItemStrips(built, frame, fitRadiusMeters, look(), style);
		}
	}

} // namespace asset_manager
