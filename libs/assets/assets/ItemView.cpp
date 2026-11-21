#include "assets/ItemView.h"

#include "assets/DetailLevels.h"

#include <utils/Log.h>
#include <vector/SVGLoader.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <numbers>
#include <span>

namespace assets {

	namespace {

		constexpr float kInfinity = std::numeric_limits<float>::infinity();

		glm::vec2 turned(glm::vec2 point, float radians) {
			const float cosine = std::cos(radians);
			const float sine = std::sin(radians);
			return {point.x * cosine - point.y * sine, point.x * sine + point.y * cosine};
		}

		Foundation::Color shaded(Foundation::Color color, float shade) {
			return {std::min(color.r * shade, 1.0F), std::min(color.g * shade, 1.0F), std::min(color.b * shade, 1.0F), color.a};
		}

		/// Triangles with 32-bit indices while a view is put together; it becomes a 16-bit mesh at the end.
		struct WorkMesh {
			std::vector<glm::vec2>		   positions;
			std::vector<Foundation::Color> colors;
			std::vector<uint32_t>		   indices;

			uint32_t add(glm::vec2 position, Foundation::Color color) {
				positions.push_back(position);
				colors.push_back(color);
				return static_cast<uint32_t>(positions.size() - 1);
			}

			void append(const renderer::TessellatedMesh& mesh) {
				const auto base = static_cast<uint32_t>(positions.size());
				for (size_t i = 0; i < mesh.vertices.size(); ++i) {
					add(mesh.vertices[i], mesh.hasColors() ? mesh.colors[i] : Foundation::Color::white());
				}
				for (const uint16_t index : mesh.indices) {
					indices.push_back(base + index);
				}
			}
		};

		/// The points `inward` of a line through `point` (distance >= 0) are kept.
		struct HalfPlane {
			glm::vec2 point{0.0F};
			glm::vec2 inward{0.0F};

			[[nodiscard]] float distance(glm::vec2 position) const { return (position.x - point.x) * inward.x + (position.y - point.y) * inward.y; }
		};

		struct ClipVertex {
			glm::vec2		  position{0.0F};
			Foundation::Color color;
		};

		/// Appends `source`'s triangles clipped to the intersection of `planes`. A triangle wholly inside keeps its
		/// vertices (shared with its neighbours); a cut one is a convex piece fanned back into triangles, its colours
		/// interpolated linearly along the cut edges.
		void clipInto(const WorkMesh& source, std::span<const HalfPlane> planes, WorkMesh& out) {
			std::vector<uint32_t> kept(source.positions.size(), std::numeric_limits<uint32_t>::max());
			const auto			  inside = [&](glm::vec2 position) {
				 return std::all_of(planes.begin(), planes.end(), [&](const HalfPlane& plane) { return plane.distance(position) >= 0.0F; });
			};
			std::vector<ClipVertex> polygon;
			std::vector<ClipVertex> next;
			for (size_t t = 0; t + 2 < source.indices.size(); t += 3) {
				const std::array<uint32_t, 3> corners = {source.indices[t], source.indices[t + 1], source.indices[t + 2]};
				if (std::all_of(corners.begin(), corners.end(), [&](uint32_t index) { return inside(source.positions[index]); })) {
					for (const uint32_t index : corners) {
						if (kept[index] == std::numeric_limits<uint32_t>::max()) {
							kept[index] = out.add(source.positions[index], source.colors[index]);
						}
						out.indices.push_back(kept[index]);
					}
					continue;
				}
				polygon.clear();
				for (const uint32_t index : corners) {
					polygon.push_back({source.positions[index], source.colors[index]});
				}
				for (const HalfPlane& plane : planes) {
					next.clear();
					for (size_t i = 0; i < polygon.size(); ++i) {
						const ClipVertex& current = polygon[i];
						const ClipVertex& following = polygon[(i + 1) % polygon.size()];
						const float		  currentSide = plane.distance(current.position);
						const float		  followingSide = plane.distance(following.position);
						if (currentSide >= 0.0F) {
							next.push_back(current);
						}
						if ((currentSide >= 0.0F) != (followingSide >= 0.0F)) {
							const float		  along = currentSide / (currentSide - followingSide);
							const auto		  mix = [&](float from, float to) { return from + (to - from) * along; };
							next.push_back(
								{current.position + (following.position - current.position) * along,
								 Foundation::Color{
									 mix(current.color.r, following.color.r),
									 mix(current.color.g, following.color.g),
									 mix(current.color.b, following.color.b),
									 mix(current.color.a, following.color.a)
								 }}
							);
						}
					}
					std::swap(polygon, next);
					if (polygon.size() < 3) {
						break;
					}
				}
				if (polygon.size() < 3) {
					continue;
				}
				const uint32_t first = out.add(polygon[0].position, polygon[0].color);
				uint32_t	   previous = out.add(polygon[1].position, polygon[1].color);
				for (size_t i = 2; i < polygon.size(); ++i) {
					const uint32_t current = out.add(polygon[i].position, polygon[i].color);
					out.indices.insert(out.indices.end(), {first, previous, current});
					previous = current;
				}
			}
		}

		/// The half-planes of a convex polygon, whichever way it is wound.
		std::vector<HalfPlane> halfPlanesOf(const std::vector<glm::vec2>& polygon) {
			float doubledArea = 0.0F;
			for (size_t i = 0; i < polygon.size(); ++i) {
				const glm::vec2& from = polygon[i];
				const glm::vec2& to = polygon[(i + 1) % polygon.size()];
				doubledArea += from.x * to.y - to.x * from.y;
			}
			const float			   sign = doubledArea >= 0.0F ? 1.0F : -1.0F;
			std::vector<HalfPlane> planes;
			for (size_t i = 0; i < polygon.size(); ++i) {
				const glm::vec2 along = polygon[(i + 1) % polygon.size()] - polygon[i];
				planes.push_back({polygon[i], glm::vec2{-along.y, along.x} * sign});
			}
			return planes;
		}

		std::vector<HalfPlane> bandPlanes(float minX, float maxX) {
			std::vector<HalfPlane> planes;
			if (std::isfinite(minX)) {
				planes.push_back({{minX, 0.0F}, {1.0F, 0.0F}});
			}
			if (std::isfinite(maxX)) {
				planes.push_back({{maxX, 0.0F}, {-1.0F, 0.0F}});
			}
			return planes;
		}

		/// A closed stroke round `ring`, built in screen space.
		void appendClosedStroke(const std::vector<glm::vec2>& ring, Foundation::Color color, float width, WorkMesh& out) {
			renderer::LoadedSVGShape shape;
			shape.paths.emplace_back(ring, true);
			shape.hasFill = false;
			shape.hasStroke = true;
			shape.strokeColor = color;
			shape.strokeWidth = width;
			renderer::TessellatedMesh mesh;
			renderer::appendShapeMesh(shape, mesh);
			out.append(mesh);
		}

		void appendConvexFill(const std::vector<glm::vec2>& ring, Foundation::Color color, WorkMesh& out) {
			if (ring.size() < 3) {
				return;
			}
			const uint32_t first = out.add(ring[0], color);
			uint32_t	   previous = out.add(ring[1], color);
			for (size_t i = 2; i < ring.size(); ++i) {
				const uint32_t current = out.add(ring[i], color);
				out.indices.insert(out.indices.end(), {first, previous, current});
				previous = current;
			}
		}

		/// One placement's visible sides, top, and outline (section 6.6), in that order.
		WorkMesh placementMesh(
			const PartsAsset& parts, uint32_t placementIndex, float turnRadians, float pixelsPerMeter, std::span<const VisibleFace> faces,
			const ViewConfig& config
		) {
			WorkMesh			  out;
			const PartPlacement&  placement = parts.placements[placementIndex];
			const PartDefinition& part = parts.parts[placement.part];
			const float			  ratio = config.heightRatio;
			const float			  height = part.topMeters - part.bottomMeters;
			const auto			  keeps = [&](const ItemDrawingShape& shape) { return shape.minPixelsPerMeter <= pixelsPerMeter; };

			for (const VisibleFace& face : faces) {
				if (face.placement != placementIndex) {
					continue;
				}
				const PartFace&					   partFace = part.faces[face.face];
				const Foundation::AffineTransform2D& matrix = face.matrix;
				const std::vector<glm::vec2>	   corners = {
					  matrix.applyToPoint({0.0F, 0.0F}),
					  matrix.applyToPoint({partFace.lengthMeters, 0.0F}),
					  matrix.applyToPoint({partFace.lengthMeters, height}),
					  matrix.applyToPoint({0.0F, height}),
				};
				if (!face.drawsArt) {
					appendConvexFill(corners, shaded(part.sideColor, face.shade), out);
					continue;
				}
				renderer::TessellatedMesh drawn;
				for (const ItemDrawingShape& shape : parts.drawings[*partFace.drawing].shapes) {
					if (keeps(shape)) {
						renderer::appendShapeMesh(
							shape.shape, drawn, {.transform = matrix, .vectorEffect = renderer::VectorEffect::NonScalingStroke, .brightness = face.shade}
						);
					}
				}
				WorkMesh art;
				art.append(drawn);
				clipInto(art, halfPlanesOf(corners), out);
			}

			// The top: drawn over the footprint's bounding box, turned with the placement and the item, then lifted.
			const std::vector<glm::vec2> footprint = turnedFootprint(parts, placementIndex, turnRadians);
			const glm::vec2				 lift{0.0F, -part.topMeters * ratio};
			if (part.topDrawing.has_value()) {
				const float		composed = turnRadians + placement.turnRadians;
				const glm::vec2 origin = turned(turned(part.boundsMin, placement.turnRadians) + placement.offsetMeters, turnRadians) + lift;
				const Foundation::AffineTransform2D matrix{
					.a = std::cos(composed), .b = std::sin(composed), .c = -std::sin(composed), .d = std::cos(composed), .e = origin.x, .f = origin.y
				};
				renderer::TessellatedMesh drawn;
				for (const ItemDrawingShape& shape : parts.drawings[*part.topDrawing].shapes) {
					if (keeps(shape)) {
						renderer::appendShapeMesh(shape.shape, drawn, {.transform = matrix, .vectorEffect = renderer::VectorEffect::NonScalingStroke});
					}
				}
				out.append(drawn);
			} else {
				std::vector<glm::vec2> top;
				for (const glm::vec2& point : footprint) {
					top.push_back(point + lift);
				}
				appendConvexFill(top, part.topColor, out);
			}

			// The outline: round the footprint projected at its bottom and its top, then round the top.
			std::vector<glm::vec2> projected;
			std::vector<glm::vec2> top;
			for (const glm::vec2& point : footprint) {
				projected.push_back(point + glm::vec2{0.0F, -part.bottomMeters * ratio});
				projected.push_back(point + lift);
				top.push_back(point + lift);
			}
			appendClosedStroke(convexHull(std::move(projected)), config.outlineColor, config.outlineWidthMeters, out);
			appendClosedStroke(top, config.outlineTopColor, config.outlineTopWidthMeters, out);
			return out;
		}

		// ─── The strip layout ───────────────────────────────────────────────────────────────────────────

		struct Edge {
			glm::vec2 from{0.0F};
			glm::vec2 to{0.0F};

			[[nodiscard]] float yAt(float x) const { return from.y + (to.y - from.y) * (x - from.x) / (to.x - from.x); }
		};

		/// One interval of the collider within a band: the trapezoid between a northern edge and the next southern one.
		struct Interval {
			Edge  north;
			Edge  south;
			float anchor = 0.0F;
			float rise = 0.0F; ///< how far the southern edge rises across the band
		};

		struct Band {
			float				  minX = 0.0F;
			float				  maxX = 0.0F;
			std::vector<Interval> intervals;
		};

		/// A strip while the layout is being made: a run of one interval in each of consecutive bands.
		struct Run {
			size_t					   firstBand = 0;
			size_t					   lastBand = 0;
			Interval				   lastInterval;
			float					   firstAnchor = 0.0F;
			float					   anchor = 0.0F;
			float					   minY = kInfinity;
			float					   maxY = -kInfinity;
			std::map<uint32_t, std::vector<size_t>> bandsByPlacement;
		};

		/// The y range of a convex polygon between two x values.
		std::pair<float, float> yRangeBetween(const std::vector<glm::vec2>& ring, float minX, float maxX) {
			float low = kInfinity;
			float high = -kInfinity;
			const auto take = [&](float y) {
				low = std::min(low, y);
				high = std::max(high, y);
			};
			for (const glm::vec2& point : ring) {
				if (point.x >= minX && point.x <= maxX) {
					take(point.y);
				}
			}
			for (const float x : {minX, maxX}) {
				const auto [top, bottom] = ySpanAt(ring, x);
				if (top <= bottom) {
					take(top);
					take(bottom);
				}
			}
			return {low, high};
		}

		bool flatOnGround(const PartDefinition& part) {
			return part.flat() && part.bottomMeters <= 0.0F;
		}

	} // namespace

	uint32_t turnQuantaFromDegrees(double degrees) {
		const auto count = static_cast<long long>(kTurnQuantaPerTurn);
		const long long rounded = std::llround(degrees / kTurnQuantumDegrees);
		return static_cast<uint32_t>(((rounded % count) + count) % count);
	}

	float turnRadiansFromQuanta(uint32_t turnQuanta) {
		return static_cast<float>(static_cast<double>(turnQuanta) * kTurnQuantumDegrees * std::numbers::pi / 180.0);
	}

	float sideShade(glm::vec2 normal, const ViewConfig& config) {
		const double azimuth = static_cast<double>(config.lightAzimuthDegrees) * std::numbers::pi / 180.0;
		const double elevation = static_cast<double>(config.lightElevationDegrees) * std::numbers::pi / 180.0;
		const double toward = static_cast<double>(normal.x) * std::sin(azimuth) - static_cast<double>(normal.y) * std::cos(azimuth);
		const double wrap = config.wrap;
		const double lit = std::max(0.0, (toward + wrap) / (1.0 + wrap));
		return static_cast<float>(config.ambient + config.diffuse * lit * std::cos(elevation));
	}

	std::vector<VisibleFace> visibleFaces(const PartsAsset& parts, float turnRadians, const ViewConfig& config) {
		std::vector<VisibleFace> faces;
		for (uint32_t placementIndex = 0; placementIndex < parts.placements.size(); ++placementIndex) {
			const PartDefinition& part = parts.parts[parts.placements[placementIndex].part];
			if (part.flat() || part.faces.empty()) {
				continue;
			}
			const std::vector<glm::vec2> footprint = turnedFootprint(parts, placementIndex, turnRadians);
			for (uint32_t index = 0; index < footprint.size(); ++index) {
				const glm::vec2 start = footprint[index];
				const glm::vec2 end = footprint[(index + 1) % footprint.size()];
				const glm::vec2 along = end - start;
				const float		length = std::hypot(along.x, along.y);
				const glm::vec2 normal = glm::vec2{along.y, -along.x} / length;
				if (!(normal.y > 1.0e-6F)) {
					continue;
				}
				// Seen from outside, the left end is the edge's end vertex: named by the winding, never by comparing x.
				const glm::vec2 left = end;
				const glm::vec2 right = start;
				const glm::vec2 unit = (right - left) / length;
				const glm::vec2 leftTop{left.x, left.y - part.topMeters * config.heightRatio};
				VisibleFace		face;
				face.placement = placementIndex;
				face.face = index;
				face.left = left;
				face.right = right;
				face.normal = normal;
				face.matrix = {.a = unit.x, .b = unit.y, .c = 0.0F, .d = config.heightRatio, .e = leftTop.x, .f = leftTop.y};
				face.shade = sideShade(normal, config);
				face.drawsArt = part.faces[index].drawing.has_value() && std::abs(right.x - left.x) >= config.faceDetailMinWidthMeters;
				faces.push_back(face);
			}
		}
		return faces;
	}

	float itemReachMeters(const PartsAsset& parts, const ViewConfig& config) {
		float reach = 0.0F;
		for (size_t i = 0; i < parts.placements.size(); ++i) {
			const float lift = parts.parts[parts.placements[i].part].topMeters * config.heightRatio;
			for (const glm::vec2& point : parts.placedFootprints[i]) {
				reach = std::max(reach, std::hypot(point.x, point.y) + lift);
			}
		}
		return reach + config.outlineHalfWidthMeters();
	}

	StripLayout stripLayout(const AssetDefinition& def, uint32_t turnQuanta, const ViewConfig& config) {
		StripLayout layout{.turnQuanta = turnQuanta, .strips = {}};
		if (def.parts == nullptr) {
			return layout;
		}
		const PartsAsset& parts = *def.parts;
		const float		  turnRadians = turnRadiansFromQuanta(turnQuanta);
		const float		  ratio = config.heightRatio;
		const float		  pad = config.outlineHalfWidthMeters();

		std::vector<std::vector<glm::vec2>> footprints(parts.placements.size());
		for (size_t i = 0; i < parts.placements.size(); ++i) {
			footprints[i] = turnedFootprint(parts, i, turnRadians);
		}
		const auto partOf = [&](size_t placement) -> const PartDefinition& { return parts.parts[parts.placements[placement].part]; };

		// The collider at the turn, each edge marked southern (its ring lies north of it) or northern.
		std::vector<std::pair<Edge, bool>> edges;
		std::vector<float>				   vertexX;
		for (const CollisionShape& collider : def.partsColliders) {
			std::vector<glm::vec2> ring;
			float				   doubledArea = 0.0F;
			for (const glm::vec2& point : collider.pointsMeters) {
				ring.push_back(turned(point, turnRadians));
				vertexX.push_back(ring.back().x);
			}
			for (size_t i = 0; i < ring.size(); ++i) {
				doubledArea += ring[i].x * ring[(i + 1) % ring.size()].y - ring[(i + 1) % ring.size()].x * ring[i].y;
			}
			for (size_t i = 0; i < ring.size(); ++i) {
				const Edge edge{ring[i], ring[(i + 1) % ring.size()]};
				// The ring lies to the left of travel turned by +90 degrees, (-along.y, along.x), times the winding's sign.
				const float along = edge.to.x - edge.from.x;
				edges.emplace_back(edge, (doubledArea >= 0.0F ? along : -along) < 0.0F);
			}
		}

		// The flat strip: every flat part on the ground, or the whole item when it has no body.
		ItemStrip flatStrip{.anchorLocalY = std::numeric_limits<float>::lowest(), .minX = kInfinity, .maxX = -kInfinity, .minY = kInfinity, .maxY = -kInfinity, .flat = true, .pieces = {}};
		for (uint32_t i = 0; i < parts.placements.size(); ++i) {
			if (footprints[i].empty() || (!edges.empty() && !flatOnGround(partOf(i)))) {
				continue;
			}
			flatStrip.pieces.push_back({.placement = i, .minX = -kInfinity, .maxX = kInfinity});
			for (const glm::vec2& point : footprints[i]) {
				flatStrip.minX = std::min(flatStrip.minX, point.x - pad);
				flatStrip.maxX = std::max(flatStrip.maxX, point.x + pad);
				flatStrip.minY = std::min(flatStrip.minY, point.y - partOf(i).topMeters * ratio - pad);
				flatStrip.maxY = std::max(flatStrip.maxY, point.y + pad);
			}
		}
		if (!flatStrip.pieces.empty()) {
			layout.strips.push_back(std::move(flatStrip));
		}
		if (edges.empty()) {
			return layout;
		}

		// Bands: boundaries at every collider vertex x, each span split evenly into the fewest bands no wider
		// than depthStripWidthMeters across which no southern edge rises by more than depthStripRiseMeters.
		constexpr float kSameX = 1.0e-5F;
		std::sort(vertexX.begin(), vertexX.end());
		vertexX.erase(std::unique(vertexX.begin(), vertexX.end(), [](float left, float right) { return right - left < kSameX; }), vertexX.end());
		const auto crosses = [](const Edge& edge, float x) { return (edge.from.x - x) * (edge.to.x - x) < 0.0F; };
		std::vector<Band> bands;
		for (size_t span = 0; span + 1 < vertexX.size(); ++span) {
			const float from = vertexX[span];
			const float to = vertexX[span + 1];
			const float width = to - from;
			const float middle = (from + to) * 0.5F;
			float		steepest = 0.0F;
			for (const auto& [edge, south] : edges) {
				if (south && crosses(edge, middle)) {
					steepest = std::max(steepest, std::abs((edge.to.y - edge.from.y) / (edge.to.x - edge.from.x)));
				}
			}
			// The slack keeps a span that is a whole number of bands (a 3 m bench, twelve 0.25 m bands) from taking one more.
			constexpr double kSlack = 1.0e-4;
			const double	 byWidth = std::ceil(static_cast<double>(width) / config.depthStripWidthMeters - kSlack);
			const double	 byRise = std::ceil(static_cast<double>(width) * steepest / config.depthStripRiseMeters - kSlack);
			const auto		 count = static_cast<size_t>(std::max({1.0, byWidth, byRise}));
			for (size_t i = 0; i < count; ++i) {
				Band band;
				band.minX = from + width * static_cast<float>(i) / static_cast<float>(count);
				band.maxX = i + 1 == count ? to : from + width * static_cast<float>(i + 1) / static_cast<float>(count);
				const float bandMiddle = (band.minX + band.maxX) * 0.5F;
				std::vector<std::pair<float, const std::pair<Edge, bool>*>> crossing;
				for (const auto& entry : edges) {
					if (crosses(entry.first, bandMiddle)) {
						crossing.emplace_back(entry.first.yAt(bandMiddle), &entry);
					}
				}
				std::sort(crossing.begin(), crossing.end(), [](const auto& left, const auto& right) { return left.first < right.first; });
				const Edge* open = nullptr;
				for (const auto& [y, entry] : crossing) {
					if (!entry->second) {
						open = &entry->first;
					} else if (open != nullptr) {
						const Edge& south = entry->first;
						band.intervals.push_back(
							{.north = *open,
							 .south = south,
							 .anchor = std::max(south.yAt(band.minX), south.yAt(band.maxX)),
							 .rise = std::abs(south.yAt(band.maxX) - south.yAt(band.minX))}
						);
						open = nullptr;
					}
				}
				bands.push_back(std::move(band));
			}
		}

		// Each part's piece in a band lies in one interval: the one holding the middle of the piece.
		struct Cell {
			size_t	 band = 0;
			size_t	 interval = 0;
			uint32_t placement = 0;
			float	 minY = 0.0F;
			float	 maxY = 0.0F;
		};
		std::vector<Cell> cells;
		constexpr float	  kNoOverlap = 1.0e-6F;
		for (uint32_t placement = 0; placement < parts.placements.size(); ++placement) {
			if (footprints[placement].empty() || flatOnGround(partOf(placement))) {
				continue;
			}
			const auto [lowest, highest] = std::minmax_element(
				footprints[placement].begin(), footprints[placement].end(), [](glm::vec2 left, glm::vec2 right) { return left.x < right.x; }
			);
			for (size_t band = 0; band < bands.size(); ++band) {
				const bool	firstBand = band == 0;
				const bool	lastBand = band + 1 == bands.size();
				const float from = firstBand ? lowest->x : std::max(bands[band].minX, lowest->x);
				const float to = lastBand ? highest->x : std::min(bands[band].maxX, highest->x);
				if (to - from <= kNoOverlap || bands[band].intervals.empty()) {
					continue;
				}
				const float x = (from + to) * 0.5F;
				const auto [top, bottom] = ySpanAt(footprints[placement], x);
				const float y = (top + bottom) * 0.5F;
				size_t		best = 0;
				float		bestDistance = kInfinity;
				for (size_t index = 0; index < bands[band].intervals.size(); ++index) {
					const Interval& interval = bands[band].intervals[index];
					const float		distance = std::max({interval.north.yAt(x) - y, y - interval.south.yAt(x), 0.0F});
					if (distance < bestDistance) {
						bestDistance = distance;
						best = index;
					}
				}
				// The outline reaches pad past the footprint in any direction, so across a steep edge it reaches further
				// than pad in y: the footprint's y range is taken over the band widened by pad, then padded.
				const auto [minY, maxY] = yRangeBetween(footprints[placement], from - pad, to + pad);
				cells.push_back({.band = band, .interval = best, .placement = placement, .minY = minY - partOf(placement).topMeters * ratio, .maxY = maxY});
			}
		}

		// Strips, merged left to right: a strip joins the run of the strip beside it in the band to its left when
		// each is the only strip of its band the other's interval touches, the southern edges of both rise by no more
		// than depthStripMergeMeters across their bands, and its anchor is within depthStripMergeMeters of the run's
		// first. Merging so happens only where the southern edge is near flat, never across a peak.
		const auto touches = [](const Interval& left, const Interval& right, float x) {
			return std::min(left.south.yAt(x), right.south.yAt(x)) - std::max(left.north.yAt(x), right.north.yAt(x)) > kNoOverlap;
		};
		std::vector<Run>	runs;
		std::vector<size_t> previousRuns; // the runs ending in the band before
		for (size_t band = 0; band < bands.size(); ++band) {
			std::vector<size_t> currentRuns;
			std::vector<size_t> intervalRun(bands[band].intervals.size(), std::numeric_limits<size_t>::max());
			for (size_t index = 0; index < bands[band].intervals.size(); ++index) {
				const bool used = std::any_of(cells.begin(), cells.end(), [&](const Cell& cell) { return cell.band == band && cell.interval == index; });
				if (!used) {
					continue;
				}
				const Interval& interval = bands[band].intervals[index];
				const float		boundary = bands[band].minX;
				std::vector<size_t> touching;
				for (const size_t run : previousRuns) {
					if (touches(runs[run].lastInterval, interval, boundary)) {
						touching.push_back(run);
					}
				}
				bool joined = false;
				if (touching.size() == 1) {
					Run&	   left = runs[touching.front()];
					const auto alsoTouching = std::count_if(bands[band].intervals.begin(), bands[band].intervals.end(), [&](const Interval& other) {
						return touches(left.lastInterval, other, boundary);
					});
					if (alsoTouching == 1 && interval.rise <= config.depthStripMergeMeters && left.lastInterval.rise <= config.depthStripMergeMeters &&
						std::abs(interval.anchor - left.firstAnchor) <= config.depthStripMergeMeters) {
						left.lastBand = band;
						left.lastInterval = interval;
						left.anchor = std::max(left.anchor, interval.anchor);
						intervalRun[index] = touching.front();
						currentRuns.push_back(touching.front());
						joined = true;
					}
				}
				if (!joined) {
					runs.push_back({.firstBand = band, .lastBand = band, .lastInterval = interval, .firstAnchor = interval.anchor, .anchor = interval.anchor});
					intervalRun[index] = runs.size() - 1;
					currentRuns.push_back(runs.size() - 1);
				}
			}
			for (const Cell& cell : cells) {
				if (cell.band != band) {
					continue;
				}
				Run& run = runs[intervalRun[cell.interval]];
				run.minY = std::min(run.minY, cell.minY - pad);
				run.maxY = std::max(run.maxY, cell.maxY + pad);
				run.bandsByPlacement[cell.placement].push_back(band);
			}
			previousRuns = std::move(currentRuns);
		}

		const size_t firstStrip = layout.strips.size();
		for (const Run& run : runs) {
			ItemStrip strip;
			strip.anchorLocalY = run.anchor;
			strip.minX = bands[run.firstBand].minX - (run.firstBand == 0 ? pad : 0.0F);
			strip.maxX = bands[run.lastBand].maxX + (run.lastBand + 1 == bands.size() ? pad : 0.0F);
			strip.minY = run.minY;
			strip.maxY = run.maxY;
			for (const auto& [placement, placementBands] : run.bandsByPlacement) {
				// Consecutive bands are one piece; the first and last band take everything beyond the collider.
				for (size_t i = 0; i < placementBands.size();) {
					size_t last = i;
					while (last + 1 < placementBands.size() && placementBands[last + 1] == placementBands[last] + 1) {
						++last;
					}
					const size_t from = placementBands[i];
					const size_t to = placementBands[last];
					strip.pieces.push_back(
						{.placement = placement,
						 .minX = from == 0 ? -kInfinity : bands[from].minX,
						 .maxX = to + 1 == bands.size() ? kInfinity : bands[to].maxX}
					);
					i = last + 1;
				}
			}
			layout.strips.push_back(std::move(strip));
		}
		std::stable_sort(layout.strips.begin() + static_cast<std::ptrdiff_t>(firstStrip), layout.strips.end(), [](const ItemStrip& left, const ItemStrip& right) {
			return left.anchorLocalY != right.anchorLocalY ? left.anchorLocalY < right.anchorLocalY : left.minX < right.minX;
		});
		return layout;
	}

	ItemView buildItemMesh(const AssetDefinition& def, uint32_t turnQuanta, uint8_t level, const ViewConfig& config) {
		ItemView view;
		view.turnQuanta = turnQuanta;
		view.level = level;
		if (def.parts == nullptr) {
			view.error = def.defName + " is not a parts def";
			return view;
		}
		if (level >= std::max<size_t>(def.detailLevels.size(), 1)) {
			view.error = def.defName + " has no detail level " + std::to_string(level);
			return view;
		}
		const PartsAsset& parts = *def.parts;
		const float		  turnRadians = turnRadiansFromQuanta(turnQuanta);
		const float		  pixelsPerMeter = def.detailLevels.empty() ? kInfinity : def.detailLevels[level];
		const StripLayout layout = stripLayout(def, turnQuanta, config);
		const std::vector<uint32_t>	   order = partsDrawOrder(parts, turnRadians);
		const std::vector<VisibleFace> faces = visibleFaces(parts, turnRadians, config);

		std::vector<WorkMesh> placementMeshes(parts.placements.size());
		for (const uint32_t placement : order) {
			if (!parts.placedFootprints[placement].empty()) {
				placementMeshes[placement] = placementMesh(parts, placement, turnRadians, pixelsPerMeter, faces, config);
			}
		}

		// Each strip's triangles in one contiguous index range, the parts order kept within it.
		WorkMesh assembled;
		for (const ItemStrip& strip : layout.strips) {
			ViewStrip& out = view.strips.emplace_back();
			out.firstIndex = static_cast<uint32_t>(assembled.indices.size());
			for (const uint32_t placement : order) {
				for (const StripPiece& piece : strip.pieces) {
					if (piece.placement == placement) {
						clipInto(placementMeshes[placement], bandPlanes(piece.minX, piece.maxX), assembled);
					}
				}
			}
			out.indexCount = static_cast<uint32_t>(assembled.indices.size()) - out.firstIndex;
			out.anchorLocalY = strip.anchorLocalY;
			out.minX = strip.minX;
			out.maxX = strip.maxX;
			out.minY = strip.minY;
			out.maxY = strip.maxY;
			out.flat = strip.flat;
		}

		view.vertexCount = static_cast<uint32_t>(assembled.positions.size());
		view.reachMeters = itemReachMeters(parts, config);
		if (view.vertexCount > kMaxViewVertices) {
			view.error = def.defName + "'s view at " + std::to_string(static_cast<double>(turnQuanta) * kTurnQuantumDegrees) + " degrees, level " +
						 std::to_string(level) + ", has " + std::to_string(view.vertexCount) + " vertices, past the " + std::to_string(kMaxViewVertices) +
						 " a mesh can index";
			view.strips.clear();
			LOG_ERROR(Engine, "%s", view.error.c_str());
			return view;
		}
		renderer::TessellatedMesh& mesh = view.mesh;
		mesh.vertices = std::move(assembled.positions);
		mesh.colors = std::move(assembled.colors);
		mesh.indices.reserve(assembled.indices.size());
		for (const uint32_t index : assembled.indices) {
			mesh.indices.push_back(static_cast<uint16_t>(index));
		}
		return view;
	}

	ItemView buildItemView(const AssetDefinition& def, uint32_t turnQuanta, uint8_t level, const ViewConfig& config) {
		ItemView view = buildItemMesh(def, turnQuanta, level, config);
		if (view.error.empty() && level == referenceDetailLevel(def.detailLevels)) {
			view.silhouette = silhouetteOfMesh(view.mesh);
		}
		return view;
	}

} // namespace assets
