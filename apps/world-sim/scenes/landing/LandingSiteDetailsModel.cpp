#include "scenes/landing/LandingSiteDetailsModel.h"

#include <worldgen/data/Biome.h>
#include <worldgen/data/GeneratedWorld.h>
#include <worldgen/data/WorldData.h>

#include <cmath>
#include <format>

namespace world_sim {

namespace {

const Foundation::Color kFresh{0.45F, 0.78F, 1.0F, 1.0F};  // drinkable blue
const Foundation::Color kSalt{0.55F, 0.75F, 0.7F, 1.0F};   // teal saltwater
const Foundation::Color kDry{0.85F, 0.7F, 0.4F, 1.0F};     // sandy rain-fed
const Foundation::Color kNeutral{0.82F, 0.82F, 0.85F, 1.0F};

bool hasField(const worldgen::GeneratedWorld& world, worldgen::WorldField f) {
	return (world.validFields & static_cast<uint32_t>(f)) != 0;
}

Foundation::Color habitabilityColor(worldgen::Habitability h) {
	switch (h) {
		case worldgen::Habitability::Easy:     return {0.4F, 0.85F, 0.45F, 1.0F};
		case worldgen::Habitability::Moderate: return {0.7F, 0.85F, 0.4F, 1.0F};
		case worldgen::Habitability::Hard:     return {0.95F, 0.7F, 0.3F, 1.0F};
		case worldgen::Habitability::Harsh:    return {0.95F, 0.4F, 0.35F, 1.0F};
	}
	return kNeutral;
}

// The headline sentence + accent for each water class. Coast is explicitly
// flagged saltwater so the player doesn't mistake it for a drinking source.
void waterVerdict(worldgen::WaterClass w, uint16_t rainMm,
                  std::string& verdict, Foundation::Color& color) {
	switch (w) {
		case worldgen::WaterClass::River:
			verdict = "Fresh water: a river runs through this site";
			color = kFresh;
			break;
		case worldgen::WaterClass::Lake:
			verdict = "Fresh water: a lake here or nearby";
			color = kFresh;
			break;
		case worldgen::WaterClass::Coastal:
			verdict = "Coastal: saltwater only (no fresh source)";
			color = kSalt;
			break;
		case worldgen::WaterClass::RainFed:
			verdict = std::format("No surface water: rain-fed ({} mm/yr)", rainMm);
			color = kDry;
			break;
	}
}

} // namespace

LandingSiteDetails buildLandingSiteDetails(
		const worldgen::GeneratedWorld& world, double latDeg, double lonDeg) {
	LandingSiteDetails out;

	out.location = std::format("{:.2f} {}, {:.2f} {}",
		std::abs(latDeg), latDeg >= 0.0 ? "N" : "S",
		std::abs(lonDeg), lonDeg >= 0.0 ? "E" : "W");

	if (!world.grid) {
		out.verdict = "No world data";
		out.verdictColor = kNeutral;
		return out;
	}

	worldgen::TileId tile = world.grid->fromLatLon(latDeg, lonDeg);

	uint16_t rainMm = 0;
	if (hasField(world, worldgen::WorldField::Precipitation) &&
	    tile < world.data.precipitation.size()) {
		rainMm = world.data.precipitation[tile];
	}

	worldgen::WaterClass water = worldgen::classifyWater(world, tile);
	waterVerdict(water, rainMm, out.verdict, out.verdictColor);

	out.habitability = worldgen::rateHabitability(world, tile, water);
	out.habitabilityText = worldgen::habitabilityToString(out.habitability);
	out.habitabilityColor = habitabilityColor(out.habitability);

	// Water section: the classification plus the drinkable/not note.
	{
		DetailSection s;
		s.header = "Water";
		s.rows.push_back({"Source", worldgen::waterClassToString(water), out.verdictColor});
		s.rows.push_back({"Drinkable",
			worldgen::isFreshwater(water) ? "Yes (fresh)"
			: water == worldgen::WaterClass::Coastal ? "No (salt)"
			: "Rain only",
			out.verdictColor});
		out.sections.push_back(std::move(s));
	}

	// Terrain section: biome + elevation relative to sea level.
	{
		DetailSection s;
		s.header = "Terrain";
		if (hasField(world, worldgen::WorldField::Biome) && tile < world.data.biome.size()) {
			s.rows.push_back({"Biome",
				worldgen::biomeToString(static_cast<worldgen::Biome>(world.data.biome[tile])),
				kNeutral});
		}
		if (hasField(world, worldgen::WorldField::Elevation) &&
		    tile < world.data.elevation.size()) {
			float relM = world.data.elevation[tile] - world.seaLevelMeters;
			s.rows.push_back({"Elevation", std::format("{:.0f} m", relM), kNeutral});
		}
		if (!s.rows.empty()) out.sections.push_back(std::move(s));
	}

	// Ice & snow: only shown when the tile carries some cryosphere.
	if (hasField(world, worldgen::WorldField::Flags) && tile < world.data.flags.size()) {
		const uint8_t fl = world.data.flags[tile];
		const bool hasThick = hasField(world, worldgen::WorldField::IceThickness) &&
		                      tile < world.data.iceThickness.size();
		const uint16_t thickM = hasThick ? world.data.iceThickness[tile] : uint16_t{0};
		const Foundation::Color kIce{0.72F, 0.85F, 0.95F, 1.0F};

		DetailSection s;
		s.header = "Ice & snow";
		if (fl & worldgen::kFlagGlacier) {
			// Thin land ice is an alpine/valley glacier; a continental-scale dome
			// (>= 300 m, the WorldStats ice-sheet threshold) is an ice sheet.
			constexpr uint16_t kIceSheetThickM = 300;
			const char* label = thickM >= kIceSheetThickM ? "Ice sheet" : "Glacier";
			s.rows.push_back({label, std::format("{} m thick", thickM), kIce});
		} else if (fl & worldgen::kFlagSeaIce) {
			s.rows.push_back({"Sea ice",
				thickM > 0 ? std::format("{} m thick", thickM) : std::string("Yes"), kIce});
		}
		if ((fl & worldgen::kFlagPermanentSnow) && !(fl & worldgen::kFlagGlacier)) {
			s.rows.push_back({"Snow", "Permanent", kIce});
		}
		if (!s.rows.empty()) out.sections.push_back(std::move(s));
	}

	// Climate section: mean temp, seasonal swing, rainfall.
	{
		DetailSection s;
		s.header = "Climate";
		if (hasField(world, worldgen::WorldField::TemperatureMean) &&
		    tile < world.data.temperatureMean.size()) {
			float meanC = world.data.temperatureMean[tile] / 10.0F;
			s.rows.push_back({"Mean temp", std::format("{:.1f} C", meanC), kNeutral});
		}
		if (hasField(world, worldgen::WorldField::TemperatureRange) &&
		    tile < world.data.temperatureRange.size()) {
			float rangeC = world.data.temperatureRange[tile] / 10.0F;
			s.rows.push_back({"Seasonal", std::format("+/- {:.1f} C", rangeC), kNeutral});
		}
		if (hasField(world, worldgen::WorldField::Precipitation) &&
		    tile < world.data.precipitation.size()) {
			s.rows.push_back({"Rainfall", std::format("{} mm/yr", rainMm), kNeutral});
		}
		if (!s.rows.empty()) out.sections.push_back(std::move(s));
	}

	return out;
}

} // namespace world_sim
