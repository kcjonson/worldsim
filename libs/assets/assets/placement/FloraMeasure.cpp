#include "FloraMeasure.h"

#include "assets/AssetDefinition.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace assets {

	namespace {

		constexpr uint8_t kMeasured = 1U;
		constexpr uint8_t kCovered = 2U;
		constexpr double  kThousand = 1000.0;
		constexpr double  kMillimetersToMeters = 0.001;

		// The cells whose centres fall in [from, to) along one axis of a grid starting at
		// origin with `count` cells, as a half-open index range.
		struct CellSpan {
			int begin = 0;
			int end = 0;
		};
		CellSpan cellSpan(double from, double to, double origin, int count) {
			const double cell = kFloraCellSizeMeters;
			const auto	 begin = static_cast<int>(std::ceil((from - origin) / cell - 0.5));
			const auto	 end = static_cast<int>(std::ceil((to - origin) / cell - 0.5));
			return {std::clamp(begin, 0, count), std::clamp(end, 0, count)};
		}

		bool contains(const FloraRect& r, double x, double y) {
			return x >= r.minX && x < r.maxX && y >= r.minY && y < r.maxY;
		}

		double overlapArea(const FloraRect& a, const FloraRect& b) {
			const double w = std::min(a.maxX, b.maxX) - std::max(a.minX, b.minX);
			const double h = std::min(a.maxY, b.maxY) - std::max(a.minY, b.minY);
			return (w > 0.0 && h > 0.0) ? w * h : 0.0;
		}

		struct WorldPoint {
			double x = 0.0;
			double y = 0.0;
		};

		// The grid of cell flags over the rect, with the crown rasteriser. Cells whose
		// centre is past the rect's far edge stay unmeasured.
		class CoverGrid {
		  public:
			explicit CoverGrid(const FloraRect& bounds)
				: rect(bounds),
				  columns(static_cast<int>(floraAxisCells(bounds.maxX - bounds.minX))),
				  rows(static_cast<int>(floraAxisCells(bounds.maxY - bounds.minY))),
				  cells(static_cast<size_t>(columns) * static_cast<size_t>(rows), 0U) {}

			void markMeasured(const FloraRect& area) {
				const CellSpan xs = cellSpan(std::max(area.minX, rect.minX), std::min(area.maxX, rect.maxX), rect.minX, columns);
				const CellSpan ys = cellSpan(std::max(area.minY, rect.minY), std::min(area.maxY, rect.maxY), rect.minY, rows);
				for (int j = ys.begin; j < ys.end; ++j) {
					uint8_t* row = &cells[static_cast<size_t>(j) * static_cast<size_t>(columns)];
					for (int i = xs.begin; i < xs.end; ++i) {
						row[i] |= kMeasured;
					}
				}
			}

			// Even-odd scanline fill of one closed ring at the cell-centre rows.
			void fillRing(const std::vector<WorldPoint>& ring) {
				if (ring.size() < 3) {
					return;
				}
				double minY = ring.front().y;
				double maxY = minY;
				for (const auto& p : ring) {
					minY = std::min(minY, p.y);
					maxY = std::max(maxY, p.y);
				}
				const CellSpan ys = cellSpan(minY, maxY, rect.minY, rows);
				for (int j = ys.begin; j < ys.end; ++j) {
					const double y = rect.minY + (j + 0.5) * kFloraCellSizeMeters;
					crossings.clear();
					for (size_t k = 0; k < ring.size(); ++k) {
						const WorldPoint& a = ring[k];
						const WorldPoint& b = ring[(k + 1) % ring.size()];
						if ((a.y <= y) != (b.y <= y)) {
							crossings.push_back(a.x + (y - a.y) * (b.x - a.x) / (b.y - a.y));
						}
					}
					std::sort(crossings.begin(), crossings.end());
					uint8_t* row = &cells[static_cast<size_t>(j) * static_cast<size_t>(columns)];
					for (size_t k = 0; k + 1 < crossings.size(); k += 2) {
						const CellSpan xs = cellSpan(
							std::max<double>(crossings[k], rect.minX), std::min<double>(crossings[k + 1], rect.maxX), rect.minX, columns
						);
						for (int i = xs.begin; i < xs.end; ++i) {
							row[i] |= kCovered;
						}
					}
				}
			}

			[[nodiscard]] double coveredFraction() const {
				size_t measured = 0;
				size_t covered = 0;
				for (const uint8_t c : cells) {
					if ((c & kMeasured) != 0U) {
						++measured;
						if ((c & kCovered) != 0U) {
							++covered;
						}
					}
				}
				return measured == 0 ? 0.0 : static_cast<double>(covered) / static_cast<double>(measured);
			}

		  private:
			FloraRect			 rect;
			int					 columns;
			int					 rows;
			std::vector<uint8_t> cells;
			std::vector<double>	 crossings;
		};

	} // namespace

	int64_t floraAxisCells(float spanMeters) {
		return std::max<int64_t>(1, static_cast<int64_t>(std::ceil(spanMeters / kFloraCellSizeMeters)));
	}

	bool floraPointWithinWorld(float x, float y) {
		return std::isfinite(x) && std::isfinite(y) && std::abs(x) <= kFloraMaxCoordMeters && std::abs(y) <= kFloraMaxCoordMeters;
	}

	bool floraRectWithinLimits(const FloraRect& rect) {
		if (!floraPointWithinWorld(rect.minX, rect.minY) || !floraPointWithinWorld(rect.maxX, rect.maxY)) {
			return false;
		}
		const float width = rect.maxX - rect.minX;
		const float height = rect.maxY - rect.minY;
		if (!std::isfinite(width) || !std::isfinite(height) || width <= 0.0F || height <= 0.0F || width > kFloraMaxSpanMeters ||
			height > kFloraMaxSpanMeters) {
			return false;
		}
		return floraAxisCells(width) * floraAxisCells(height) <= kFloraMaxCells;
	}

	bool isTreeDef(const AssetDefinition& def) {
		const auto& groups = def.placement.groups;
		return std::find(groups.begin(), groups.end(), kTreeGroup) != groups.end();
	}

	double woodPerTree(const AssetDefinition& def) {
		if (!def.capabilities.harvestable.has_value() || def.capabilities.harvestable->yieldDefName != "Wood") {
			return 0.0;
		}
		const HarvestableCapability& h = *def.capabilities.harvestable;
		if (h.totalResourceMax > 0) {
			return (static_cast<double>(h.totalResourceMin) + static_cast<double>(h.totalResourceMax)) * 0.5;
		}
		return (static_cast<double>(h.amountMin) + static_cast<double>(h.amountMax)) * 0.5;
	}

	FloraMeasurement measureFlora(
		const FloraRect&									   rect,
		const std::vector<FloraRect>&						   loadedAreas,
		const std::vector<const PlacedEntity*>&				   entities,
		const std::unordered_map<std::string, FloraDefTraits>& traits
	) {
		if (!floraRectWithinLimits(rect)) {
			return {};
		}
		FloraMeasurement out;
		out.rect = rect;
		out.rectAreaM2 = rect.area();

		CoverGrid grid(rect);
		for (const FloraRect& area : loadedAreas) {
			out.measuredAreaM2 += overlapArea(rect, area);
			grid.markMeasured(area);
		}

		std::map<std::string, FloraDefMeasure> treesByDef;
		std::map<std::string, FloraDefMeasure> othersByDef;
		std::vector<WorldPoint>				   worldRing;
		for (const PlacedEntity* entity : entities) {
			if (!entity->isTallOccluder) {
				continue;
			}
			const auto					traitIt = traits.find(entity->defName);
			const FloraDefTraits* const def = traitIt != traits.end() ? &traitIt->second : nullptr;
			const bool					isTree = def != nullptr && def->isTree;

			const double x = entity->position.x;
			const double y = entity->position.y;
			const bool	 counted = contains(rect, x, y) &&
								 std::any_of(loadedAreas.begin(), loadedAreas.end(), [&](const FloraRect& a) { return contains(a, x, y); });
			if (counted) {
				FloraDefMeasure& m = (isTree ? treesByDef : othersByDef)[entity->defName];
				m.defName = entity->defName;
				m.count++;
				m.woodPerTree = def != nullptr ? def->woodPerTree : 0.0;
				if (isTree && !def->ageClassNames.empty()) {
					if (m.ageClasses.empty()) {
						for (const std::string& name : def->ageClassNames) {
							m.ageClasses.push_back({.name = name});
						}
					}
					if (entity->traits.ageClass < m.ageClasses.size()) {
						m.ageClasses[entity->traits.ageClass].count++;
					}
				}
			}

			if (!isTree || def->rings == nullptr) {
				continue;
			}
			// world = R(rotation) * (local * scale) + position, the static instance transform
			const double c = std::cos(entity->rotation);
			const double s = std::sin(entity->rotation);
			const double k = entity->scale * kMillimetersToMeters;
			for (const geometry::Ring& ring : *def->rings) {
				worldRing.clear();
				for (const auto& p : ring) {
					const double lx = static_cast<double>(p.x) * k;
					const double ly = static_cast<double>(p.y) * k;
					worldRing.push_back({lx * c - ly * s + x, lx * s + ly * c + y});
				}
				grid.fillRing(worldRing);
			}
		}

		out.canopyCover = grid.coveredFraction();
		const double perThousand = out.measuredAreaM2 > 0.0 ? kThousand / out.measuredAreaM2 : 0.0;
		double		 wood = 0.0;
		for (auto& [name, m] : treesByDef) {
			m.perThousandM2 = m.count * perThousand;
			for (FloraAgeMeasure& age : m.ageClasses) {
				age.perThousandM2 = age.count * perThousand;
			}
			m.woodPerThousandM2 = m.count * m.woodPerTree * perThousand;
			out.treeCount += m.count;
			wood += m.count * m.woodPerTree;
			out.defs.push_back(std::move(m));
		}
		for (auto& [name, m] : othersByDef) {
			m.perThousandM2 = m.count * perThousand;
			m.woodPerThousandM2 = m.count * m.woodPerTree * perThousand;
			out.otherTallOccluders.push_back(std::move(m));
		}
		out.treesPerThousandM2 = out.treeCount * perThousand;
		out.woodPerThousandM2 = wood * perThousand;
		return out;
	}

	std::vector<FloraTreeSighting> nearestTreesOfAge(
		glm::vec2											   near,
		const std::string&									   ageClass,
		const std::string&									   defName,
		size_t											   limit,
		const std::vector<const PlacedEntity*>&				   entities,
		const std::unordered_map<std::string, FloraDefTraits>& traits
	) {
		std::vector<FloraTreeSighting> found;
		for (const PlacedEntity* entity : entities) {
			if (!entity->isTallOccluder || (!defName.empty() && entity->defName != defName)) {
				continue;
			}
			const auto traitIt = traits.find(entity->defName);
			if (traitIt == traits.end() || !traitIt->second.isTree) {
				continue;
			}
			const std::vector<std::string>& names = traitIt->second.ageClassNames;
			if (entity->traits.ageClass >= names.size() || names[entity->traits.ageClass] != ageClass) {
				continue;
			}
			const double dx = static_cast<double>(entity->position.x) - near.x;
			const double dy = static_cast<double>(entity->position.y) - near.y;
			found.push_back({entity->defName, entity->position, ageClass, entity->traits.variant, std::sqrt(dx * dx + dy * dy)});
		}
		const size_t kept = std::min(limit, found.size());
		std::partial_sort(found.begin(), found.begin() + static_cast<std::ptrdiff_t>(kept), found.end(), [](const FloraTreeSighting& a, const FloraTreeSighting& b) {
			return a.distance < b.distance;
		});
		found.resize(kept);
		return found;
	}

} // namespace assets
