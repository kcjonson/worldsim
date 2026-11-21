#include "assets/ItemView.h"

#include "assets/DetailLevels.h"

#include <core/Vec2i64.h>
#include <utils/Log.h>
#include <vector/SVGLoader.h>


#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <numbers>
#include <optional>
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

		constexpr uint32_t kNone = std::numeric_limits<uint32_t>::max();

		/// A vertex of a triangle being clipped. A source vertex has `from` its index in the source and `to` kNone. A cut
		/// vertex made on a source edge (a band edge's exact cut) is on the edge from `from` to `to`; one made by
		/// interpolating a polygon edge lies on an earlier plane's line instead and is not on a source edge.
		enum class Place : uint8_t { Source, OnSourceEdge, OnPlane };

		struct ClipVertex {
			glm::vec2		  position{0.0F};
			Foundation::Color color;
			uint32_t		  from = kNone;
			uint32_t		  to = kNone;
			Place			  place = Place::Source;
		};

		Foundation::Color mixed(const Foundation::Color& from, const Foundation::Color& to, float along) {
			const auto mix = [&](float first, float second) { return first + (second - first) * along; };
			return {mix(from.r, to.r), mix(from.g, to.g), mix(from.b, to.b), mix(from.a, to.a)};
		}

		/// The source edge a polygon edge lies along, when it lies along one: both ends source vertices (an edge of the
		/// triangle), a source vertex and a cut on an edge of its own, or two cuts on the same source edge.
		std::optional<std::pair<uint32_t, uint32_t>> sourceEdgeOf(const ClipVertex& current, const ClipVertex& following) {
			const bool currentSource = current.place == Place::Source;
			const bool followingSource = following.place == Place::Source;
			if (currentSource && followingSource) {
				return std::make_pair(current.from, following.from);
			}
			if (currentSource || followingSource) {
				const ClipVertex& vertex = currentSource ? current : following;
				const ClipVertex& cut = currentSource ? following : current;
				if (cut.place == Place::OnSourceEdge && (vertex.from == cut.from || vertex.from == cut.to)) {
					return std::make_pair(cut.from, cut.to);
				}
				return std::nullopt;
			}
			if (current.place == Place::OnSourceEdge && following.place == Place::OnSourceEdge && std::minmax(current.from, current.to) == std::minmax(following.from, following.to)) {
				return std::make_pair(current.from, current.to);
			}
			return std::nullopt;
		}

		/// Where the edge from `current` to `following` crosses a half-plane's line. On a vertical line (a band edge) an
		/// edge along a source edge is cut from that source edge, its ends in a fixed order, at exactly the line's x, so the
		/// strips either side of a band edge make bit-identical vertices and no hairline opens between them. Any other
		/// cut interpolates the polygon edge and is marked as on no source edge.
		ClipVertex cutBetween(const ClipVertex& current, const ClipVertex& following, float currentSide, float followingSide, const HalfPlane& plane,
							  const WorkMesh& source) {
			const std::optional<std::pair<uint32_t, uint32_t>> edge = plane.inward.y == 0.0F ? sourceEdgeOf(current, following) : std::nullopt;
			if (edge.has_value() && source.positions[edge->first].x != source.positions[edge->second].x) {
				auto [first, second] = *edge;
				const glm::vec2& firstPosition = source.positions[first];
				const glm::vec2& secondPosition = source.positions[second];
				if (secondPosition.x < firstPosition.x || (secondPosition.x == firstPosition.x && secondPosition.y < firstPosition.y)) {
					std::swap(first, second);
				}
				const glm::dvec2 low(source.positions[first]);
				const glm::dvec2 high(source.positions[second]);
				const double	 along = (static_cast<double>(plane.point.x) - low.x) / (high.x - low.x);
				return {
					{plane.point.x, static_cast<float>(low.y + (high.y - low.y) * along)},
					mixed(source.colors[first], source.colors[second], static_cast<float>(along)),
					first,
					second,
					Place::OnSourceEdge,
				};
			}
			const float along = currentSide / (currentSide - followingSide);
			return {current.position + (following.position - current.position) * along, mixed(current.color, following.color, along), kNone, kNone, Place::OnPlane};
		}


		/// Appends `source`'s triangles clipped to the intersection of `planes`. A triangle wholly inside keeps its
		/// vertices (shared with its neighbours); a cut one is a convex piece fanned back into triangles, its colours
		/// interpolated linearly along the cut edges.
		void clipInto(const WorkMesh& source, std::span<const HalfPlane> planes, WorkMesh& out) {
			std::vector<uint32_t>	kept(source.positions.size(), kNone);
			std::vector<ClipVertex> polygon;
			std::vector<ClipVertex> next;
			for (size_t t = 0; t + 2 < source.indices.size(); t += 3) {
				const std::array<uint32_t, 3> corners = {source.indices[t], source.indices[t + 1], source.indices[t + 2]};
				// Wholly outside one plane (for a band, its x range misses the band's): nothing to clip.
				bool outside = false;
				bool inside = true;
				for (const HalfPlane& plane : planes) {
					int in = 0;
					for (const uint32_t index : corners) {
						in += plane.distance(source.positions[index]) >= 0.0F ? 1 : 0;
					}
					outside = outside || in == 0;
					inside = inside && in == 3;
				}
				if (outside) {
					continue;
				}
				if (inside) {
					for (const uint32_t index : corners) {
						if (kept[index] == kNone) {
							kept[index] = out.add(source.positions[index], source.colors[index]);
						}
						out.indices.push_back(kept[index]);
					}
					continue;
				}
				polygon.clear();
				for (const uint32_t index : corners) {
					polygon.push_back({source.positions[index], source.colors[index], index, kNone, Place::Source});
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
							next.push_back(cutBetween(current, following, currentSide, followingSide, plane, source));
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
					// A face too narrow for its drawing takes the drawing's average colour, so it doesn't pop to another one.
					const Foundation::Color plain = partFace.drawing.has_value() ? parts.drawings[*partFace.drawing].averageColor : part.sideColor;
					appendConvexFill(corners, shaded(plain, face.shade), out);
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

			// The top: drawn over the footprint's bounding box, turned with the placement and the item, lifted, and clipped
			// to the lifted footprint, as a side's art is clipped to its face.
			const std::vector<glm::vec2> footprint = turnedFootprint(parts, placementIndex, turnRadians);
			const glm::vec2				 lift{0.0F, -part.topMeters * ratio};
			std::vector<glm::vec2>		 top;
			for (const glm::vec2& point : footprint) {
				top.push_back(point + lift);
			}
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
				WorkMesh art;
				art.append(drawn);
				clipInto(art, halfPlanesOf(top), out);
			} else {
				appendConvexFill(top, part.topColor, out);
			}

			// The outline: round the footprint projected at its bottom and its top, then round the top.
			std::vector<glm::vec2> projected;
			for (const glm::vec2& point : footprint) {
				projected.push_back(point + glm::vec2{0.0F, -part.bottomMeters * ratio});
				projected.push_back(point + lift);
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

		/// A collider edge at the turn: southern when its ring lies north of it.
		struct ColliderEdge {
			Edge edge;
			bool south = false;
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

		/// A part's piece in a band, in the interval holding the middle of the piece, with its y range.
		struct Cell {
			size_t	 band = 0;
			size_t	 interval = 0;
			uint32_t placement = 0;
			float	 minY = 0.0F;
			float	 maxY = 0.0F;
		};

		/// A strip while the layout is being made: a run of one interval in each of consecutive bands.
		struct Run {
			size_t									firstBand = 0;
			size_t									lastBand = 0;
			Interval								lastInterval;
			float									leastAnchor = 0.0F;
			float									greatestAnchor = 0.0F;
			float									minY = kInfinity;
			float									maxY = -kInfinity;
			std::map<uint32_t, std::vector<size_t>> bandsByPlacement;
		};

		constexpr float kNoOverlap = 1.0e-6F;

		/// The y range of a convex polygon between two x values.
		std::pair<float, float> yRangeBetween(const std::vector<glm::vec2>& ring, float minX, float maxX) {
			float	   low = kInfinity;
			float	   high = -kInfinity;
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

		std::pair<float, float> xRangeOf(const std::vector<glm::vec2>& ring) {
			const auto [lowest, highest] = std::minmax_element(ring.begin(), ring.end(), [](glm::vec2 left, glm::vec2 right) { return left.x < right.x; });
			return {lowest->x, highest->x};
		}

		bool flatOnGround(const PartDefinition& part) {
			return part.flat() && part.bottomMeters <= 0.0F;
		}

		/// The def's collider rings turned, as edges, and every ring vertex's x.
		std::vector<ColliderEdge> colliderEdges(const AssetDefinition& def, float turnRadians, std::vector<float>& vertexX) {
			std::vector<ColliderEdge> edges;
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
					edges.push_back({edge, (doubledArea >= 0.0F ? along : -along) < 0.0F});
				}
			}
			return edges;
		}

		bool crosses(const Edge& edge, float x) {
			return (edge.from.x - x) * (edge.to.x - x) < 0.0F;
		}

		/// Bands: boundaries at every collider vertex x, each span split evenly into the fewest bands no wider than
		/// depthStripWidthMeters across which no southern edge rises by more than depthStripRiseMeters; and each band's
		/// intervals.
		std::vector<Band> bandsOf(const std::vector<ColliderEdge>& edges, std::vector<float> vertexX, const ViewConfig& config) {
			constexpr float kSameX = 1.0e-5F;
			std::sort(vertexX.begin(), vertexX.end());
			vertexX.erase(std::unique(vertexX.begin(), vertexX.end(), [](float left, float right) { return right - left < kSameX; }), vertexX.end());
			std::vector<Band> bands;
			for (size_t span = 0; span + 1 < vertexX.size(); ++span) {
				const float from = vertexX[span];
				const float to = vertexX[span + 1];
				const float width = to - from;
				const float middle = (from + to) * 0.5F;
				float		steepest = 0.0F;
				for (const ColliderEdge& entry : edges) {
					if (entry.south && crosses(entry.edge, middle)) {
						steepest = std::max(steepest, std::abs((entry.edge.to.y - entry.edge.from.y) / (entry.edge.to.x - entry.edge.from.x)));
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
					std::vector<std::pair<float, const ColliderEdge*>> crossing;
					for (const ColliderEdge& entry : edges) {
						if (crosses(entry.edge, bandMiddle)) {
							crossing.emplace_back(entry.edge.yAt(bandMiddle), &entry);
						}
					}
					std::sort(crossing.begin(), crossing.end(), [](const auto& left, const auto& right) { return left.first < right.first; });
					const Edge* open = nullptr;
					for (const auto& [y, entry] : crossing) {
						if (!entry->south) {
							open = &entry->edge;
						} else if (open != nullptr) {
							const Edge& south = entry->edge;
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
			return bands;
		}

		/// Each part with height (or raised and flat) meets a band in one convex piece, which lies in one interval.
		std::vector<Cell> cellsOf(
			const PartsAsset& parts, const std::vector<std::vector<glm::vec2>>& footprints, const std::vector<Band>& bands, const ViewConfig& config
		) {
			const float		  pad = config.outlineHalfWidthMeters();
			std::vector<Cell> cells;
			for (uint32_t placement = 0; placement < parts.placements.size(); ++placement) {
				const PartDefinition& part = parts.parts[parts.placements[placement].part];
				if (footprints[placement].empty() || flatOnGround(part)) {
					continue;
				}
				const auto [lowest, highest] = xRangeOf(footprints[placement]);
				for (size_t band = 0; band < bands.size(); ++band) {
					const float from = band == 0 ? lowest : std::max(bands[band].minX, lowest);
					const float to = band + 1 == bands.size() ? highest : std::min(bands[band].maxX, highest);
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
					// than pad in y: the footprint's y range is taken over the piece widened by pad, then padded.
					const auto [minY, maxY] = yRangeBetween(footprints[placement], from - pad, to + pad);
					cells.push_back({.band = band, .interval = best, .placement = placement, .minY = minY - part.topMeters * config.heightRatio - pad, .maxY = maxY + pad});
				}
			}
			return cells;
		}

		/// Strips, merged left to right: a strip joins the run of the strip beside it in the band to its left when each
		/// is the only strip of its band the other's interval touches, the southern edges of both rise by no more than
		/// depthStripMergeMeters across their bands, and the run's anchors with its own spread by no more than
		/// depthStripMergeMeters. Merging so happens only where the southern edge is near flat, never across a peak.
		std::vector<Run> runsOf(const std::vector<Band>& bands, const std::vector<Cell>& cells, const ViewConfig& config) {
			const float merge = config.depthStripMergeMeters;
			const auto	touches = [](const Interval& left, const Interval& right, float x) {
				 return std::min(left.south.yAt(x), right.south.yAt(x)) - std::max(left.north.yAt(x), right.north.yAt(x)) > kNoOverlap;
			};
			std::vector<Run>	runs;
			std::vector<size_t> previousRuns; // the runs ending in the band before
			for (size_t band = 0; band < bands.size(); ++band) {
				std::vector<size_t> currentRuns;
				std::vector<size_t> intervalRun(bands[band].intervals.size(), std::numeric_limits<size_t>::max());
				const float			boundary = bands[band].minX;
				for (size_t index = 0; index < bands[band].intervals.size(); ++index) {
					if (std::none_of(cells.begin(), cells.end(), [&](const Cell& cell) { return cell.band == band && cell.interval == index; })) {
						continue;
					}
					const Interval&		interval = bands[band].intervals[index];
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
						const float spread = std::max(left.greatestAnchor, interval.anchor) - std::min(left.leastAnchor, interval.anchor);
						if (alsoTouching == 1 && interval.rise <= merge && left.lastInterval.rise <= merge && spread <= merge) {
							left.lastBand = band;
							left.lastInterval = interval;
							left.leastAnchor = std::min(left.leastAnchor, interval.anchor);
							left.greatestAnchor = std::max(left.greatestAnchor, interval.anchor);
							intervalRun[index] = touching.front();
							currentRuns.push_back(touching.front());
							joined = true;
						}
					}
					if (!joined) {
						runs.push_back(
							{.firstBand = band, .lastBand = band, .lastInterval = interval, .leastAnchor = interval.anchor, .greatestAnchor = interval.anchor}
						);
						intervalRun[index] = runs.size() - 1;
						currentRuns.push_back(runs.size() - 1);
					}
				}
				for (const Cell& cell : cells) {
					if (cell.band == band) {
						Run& run = runs[intervalRun[cell.interval]];
						run.minY = std::min(run.minY, cell.minY);
						run.maxY = std::max(run.maxY, cell.maxY);
						run.bandsByPlacement[cell.placement].push_back(band);
					}
				}
				previousRuns = std::move(currentRuns);
			}
			return runs;
		}

		/// Each run as a strip. A placement's consecutive bands in it are one piece; the first and last band take
		/// everything beyond the collider, and a part's first and last piece reach its footprint's extent plus the
		/// outline's half width, so an outline is never cut where a footprint ends on or near a band edge.
		std::vector<ItemStrip> stripsOf(
			const std::vector<Run>& runs, const std::vector<Band>& bands, const std::vector<std::vector<glm::vec2>>& footprints, const ViewConfig& config
		) {
			const float								  pad = config.outlineHalfWidthMeters();
			std::map<uint32_t, std::pair<size_t, size_t>> placementBands; // each placement's first and last band
			for (const Run& run : runs) {
				for (const auto& [placement, runBands] : run.bandsByPlacement) {
					const auto found = placementBands.find(placement);
					const auto low = runBands.front();
					const auto high = runBands.back();
					if (found == placementBands.end()) {
						placementBands.emplace(placement, std::make_pair(low, high));
					} else {
						found->second = {std::min(found->second.first, low), std::max(found->second.second, high)};
					}
				}
			}
			std::vector<ItemStrip> strips;
			for (const Run& run : runs) {
				ItemStrip strip;
				strip.anchorLocalY = run.greatestAnchor;
				strip.minX = bands[run.firstBand].minX - (run.firstBand == 0 ? pad : 0.0F);
				strip.maxX = bands[run.lastBand].maxX + (run.lastBand + 1 == bands.size() ? pad : 0.0F);
				strip.minY = run.minY;
				strip.maxY = run.maxY;
				strip.bandMinX = run.firstBand == 0 ? -kInfinity : bands[run.firstBand].minX;
				strip.bandMaxX = run.lastBand + 1 == bands.size() ? kInfinity : bands[run.lastBand].maxX;
				for (const auto& [placement, runBands] : run.bandsByPlacement) {
					const auto [extentMin, extentMax] = xRangeOf(footprints[placement]);
					const auto [firstBand, lastBand] = placementBands.at(placement);
					for (size_t i = 0; i < runBands.size();) {
						size_t last = i;
						while (last + 1 < runBands.size() && runBands[last + 1] == runBands[last] + 1) {
							++last;
						}
						const size_t from = runBands[i];
						const size_t to = runBands[last];
						StripPiece	 piece{.placement = placement, .minX = bands[from].minX, .maxX = bands[to].maxX};
						if (from == 0) {
							piece.minX = -kInfinity;
						} else if (from == firstBand) {
							piece.minX = std::min(piece.minX, extentMin - pad);
							strip.minX = std::min(strip.minX, piece.minX);
						}
						if (to + 1 == bands.size()) {
							piece.maxX = kInfinity;
						} else if (to == lastBand) {
							piece.maxX = std::max(piece.maxX, extentMax + pad);
							strip.maxX = std::max(strip.maxX, piece.maxX);
						}
						strip.pieces.push_back(piece);
						i = last + 1;
					}
				}
				strips.push_back(std::move(strip));
			}
			return strips;
		}

		/// A polygon clipped to minX <= x <= maxX (Sutherland-Hodgman against the two lines).
		std::vector<glm::vec2> clippedToColumn(const std::vector<glm::vec2>& polygon, float minX, float maxX) {
			std::vector<glm::vec2> clipped = polygon;
			for (const float sign : {1.0F, -1.0F}) {
				const float			   bound = sign > 0.0F ? minX : maxX;
				const std::vector<glm::vec2> input = std::move(clipped);
				clipped.clear();
				if (!std::isfinite(bound)) {
					clipped = input;
					continue;
				}
				for (size_t i = 0; i < input.size(); ++i) {
					const glm::vec2 from = input[i];
					const glm::vec2 to = input[(i + 1) % input.size()];
					const bool		fromInside = sign * (from.x - bound) >= 0.0F;
					const bool		toInside = sign * (to.x - bound) >= 0.0F;
					if (fromInside) {
						clipped.push_back(from);
					}
					if (fromInside != toInside) {
						const float fraction = (bound - from.x) / (to.x - from.x);
						clipped.push_back(from + ((to - from) * fraction));
					}
				}
			}
			return clipped;
		}

		/// How far a point lies outside a convex polygon; 0 inside or on it.
		float distanceOutside(const std::vector<glm::vec2>& convex, glm::vec2 point) {
			if (convex.empty()) {
				return kInfinity;
			}
			float nearest = kInfinity;
			bool  left = false;
			bool  right = false;
			for (size_t i = 0; i < convex.size(); ++i) {
				const glm::vec2 from = convex[i];
				const glm::vec2 to = convex[(i + 1) % convex.size()];
				const glm::vec2 along = to - from;
				const float		side = (along.x * (point.y - from.y)) - (along.y * (point.x - from.x));
				left = left || side > 0.0F;
				right = right || side < 0.0F;
				const float lengthSquared = (along.x * along.x) + (along.y * along.y);
				const float fraction = lengthSquared > 0.0F ? std::clamp(((point.x - from.x) * along.x + (point.y - from.y) * along.y) / lengthSquared, 0.0F, 1.0F) : 0.0F;
				const glm::vec2 closest = from + (along * fraction);
				nearest = std::min(nearest, std::hypot(point.x - closest.x, point.y - closest.y));
			}
			return left && right ? nearest : 0.0F;
		}

		/// The silhouette's rings cut at every strip's band edges into open lines, each given to the strip whose band
		/// holds it; in a band of several strips (a notch's two sides, or the flat strip), to the strip with the nearest
		/// region (stripRegions), equal distances going to the strip with the lesser anchor. A cut lies about half a
		/// stroke outside every region, since the rings include the outline. Consecutive cuts given to one strip stay one
		/// line.
		std::vector<std::vector<std::vector<glm::vec2>>> stripLinesOf(
			const PartsAsset& parts, const StripLayout& layout, float turnRadians, const ViewConfig& config, const std::vector<geometry::Ring>& rings
		) {
			std::vector<std::vector<std::vector<glm::vec2>>> lines(layout.strips.size());
			if (layout.strips.empty()) {
				return lines;
			}
			std::vector<float> cuts;
			for (const ItemStrip& strip : layout.strips) {
				for (const float x : {strip.bandMinX, strip.bandMaxX}) {
					if (std::isfinite(x)) {
						cuts.push_back(x);
					}
				}
			}
			std::sort(cuts.begin(), cuts.end());
			cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
			const std::vector<std::vector<std::vector<glm::vec2>>> regions = stripRegions(parts, layout, turnRadians, config);

			std::vector<uint32_t> candidates;
			const auto			  ownerAt = [&](glm::vec2 point) {
				   candidates.clear();
				   for (uint32_t strip = 0; strip < layout.strips.size(); ++strip) {
					   if (point.x >= layout.strips[strip].bandMinX && point.x <= layout.strips[strip].bandMaxX && !layout.strips[strip].pieces.empty()) {
						   candidates.push_back(strip);
					   }
				   }
				   if (candidates.size() == 1) {
					   return candidates.front();
				   }
				   // Candidates come in layout order, ascending anchor, so the strict comparison keeps the lesser anchor.
				   uint32_t best = candidates.empty() ? 0 : candidates.front();
				   float	bestDistance = kInfinity;
				   for (const uint32_t strip : candidates) {
					   const float distance = distanceOutsideStrip(regions[strip], point);
					   if (distance < bestDistance) {
						   bestDistance = distance;
						   best = strip;
					   }
				   }
				   return best;
			};

			struct Cut {
				glm::vec2 from;
				glm::vec2 to;
				uint32_t  strip = 0;
			};
			for (const geometry::Ring& ring : rings) {
				if (ring.size() < 2) {
					continue;
				}
				std::vector<Cut> pieces;
				for (size_t i = 0; i < ring.size(); ++i) {
					const Foundation::Vec2 fromMetres = geometry::dequantize(ring[i]);
					const Foundation::Vec2 toMetres = geometry::dequantize(ring[(i + 1) % ring.size()]);
					const glm::vec2		   from{fromMetres.x, fromMetres.y};
					const glm::vec2		   to{toMetres.x, toMetres.y};
					const float			   lowX = std::min(from.x, to.x);
					const float			   highX = std::max(from.x, to.x);
					auto				   cut = std::upper_bound(cuts.begin(), cuts.end(), lowX);
					std::vector<glm::vec2> stops{from};
					std::vector<glm::vec2> inner;
					for (; cut != cuts.end() && *cut < highX; ++cut) {
						const float fraction = (*cut - from.x) / (to.x - from.x);
						inner.push_back(from + ((to - from) * fraction));
					}
					if (from.x > to.x) {
						std::reverse(inner.begin(), inner.end());
					}
					stops.insert(stops.end(), inner.begin(), inner.end());
					stops.push_back(to);
					for (size_t stop = 0; stop + 1 < stops.size(); ++stop) {
						pieces.push_back({.from = stops[stop], .to = stops[stop + 1], .strip = ownerAt((stops[stop] + stops[stop + 1]) * 0.5F)});
					}
				}
				// Start where the owner changes, so no line wraps round the ring's first point; a ring all one strip's
				// is one line that closes on itself.
				size_t start = 0;
				for (size_t i = 0; i < pieces.size(); ++i) {
					if (pieces[i].strip != pieces[(i + pieces.size() - 1) % pieces.size()].strip) {
						start = i;
						break;
					}
				}
				std::vector<glm::vec2>* line = nullptr;
				uint32_t				lineStrip = 0;
				for (size_t step = 0; step < pieces.size(); ++step) {
					const Cut& piece = pieces[(start + step) % pieces.size()];
					if (line == nullptr || piece.strip != lineStrip) {
						line = &lines[piece.strip].emplace_back(std::vector<glm::vec2>{piece.from});
						lineStrip = piece.strip;
					}
					line->push_back(piece.to);
				}
			}
			return lines;
		}

	} // namespace

	std::vector<std::vector<std::vector<glm::vec2>>> stripRegions(const PartsAsset& parts, const StripLayout& layout, float turnRadians, const ViewConfig& config) {
		std::vector<std::vector<std::vector<glm::vec2>>> regions(layout.strips.size());
		for (size_t strip = 0; strip < layout.strips.size(); ++strip) {
			for (const StripPiece& piece : layout.strips[strip].pieces) {
				const PartDefinition&		 part = parts.parts[parts.placements[piece.placement].part];
				const std::vector<glm::vec2> slice = clippedToColumn(turnedFootprint(parts, piece.placement, turnRadians), piece.minX, piece.maxX);
				std::vector<glm::vec2>		 lifted;
				for (const glm::vec2& point : slice) {
					lifted.emplace_back(point.x, point.y - part.bottomMeters * config.heightRatio);
					lifted.emplace_back(point.x, point.y - part.topMeters * config.heightRatio);
				}
				regions[strip].push_back(convexHull(std::move(lifted)));
			}
		}
		return regions;
	}

	float distanceOutsideStrip(const std::vector<std::vector<glm::vec2>>& region, glm::vec2 point) {
		float nearest = kInfinity;
		for (const std::vector<glm::vec2>& polygon : region) {
			nearest = std::min(nearest, distanceOutside(polygon, point));
		}
		return nearest;
	}

	uint32_t turnQuantaFromDegrees(double degrees) {
		const auto count = static_cast<long long>(kTurnQuantaPerTurn);
		const long long rounded = std::llround(degrees / kTurnQuantumDegrees);
		return static_cast<uint32_t>(((rounded % count) + count) % count);
	}

	float turnRadiansFromQuanta(uint32_t turnQuanta) {
		return static_cast<float>(static_cast<double>(turnQuanta) * kTurnQuantumDegrees * std::numbers::pi / 180.0);
	}

	uint32_t turnQuantaFromRadians(float radians) {
		return turnQuantaFromDegrees(static_cast<double>(radians) * 180.0 / std::numbers::pi);
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
		const float		  pad = config.outlineHalfWidthMeters();

		std::vector<std::vector<glm::vec2>> footprints(parts.placements.size());
		for (size_t i = 0; i < parts.placements.size(); ++i) {
			footprints[i] = turnedFootprint(parts, i, turnRadians);
		}
		std::vector<float>				vertexX;
		const std::vector<ColliderEdge> edges = colliderEdges(def, turnRadians, vertexX);

		// The flat strip: every flat part on the ground, or the whole item when it has no body.
		ItemStrip flatStrip{
			.anchorLocalY = std::numeric_limits<float>::lowest(), .minX = kInfinity, .maxX = -kInfinity, .minY = kInfinity, .maxY = -kInfinity, .bandMinX = -kInfinity, .bandMaxX = kInfinity, .flat = true, .pieces = {}
		};
		for (uint32_t i = 0; i < parts.placements.size(); ++i) {
			const PartDefinition& part = parts.parts[parts.placements[i].part];
			if (footprints[i].empty() || (!edges.empty() && !flatOnGround(part))) {
				continue;
			}
			flatStrip.pieces.push_back({.placement = i, .minX = -kInfinity, .maxX = kInfinity});
			for (const glm::vec2& point : footprints[i]) {
				flatStrip.minX = std::min(flatStrip.minX, point.x - pad);
				flatStrip.maxX = std::max(flatStrip.maxX, point.x + pad);
				flatStrip.minY = std::min(flatStrip.minY, point.y - part.topMeters * config.heightRatio - pad);
				flatStrip.maxY = std::max(flatStrip.maxY, point.y + pad);
			}
		}
		if (!flatStrip.pieces.empty()) {
			layout.strips.push_back(std::move(flatStrip));
		}
		if (edges.empty()) {
			return layout;
		}

		const std::vector<Band> bands = bandsOf(edges, std::move(vertexX), config);
		const std::vector<Cell> cells = cellsOf(parts, footprints, bands, config);
		const std::vector<Run>	runs = runsOf(bands, cells, config);
		const size_t			firstStrip = layout.strips.size();
		for (ItemStrip& strip : stripsOf(runs, bands, footprints, config)) {
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
			view.silhouette.silhouette = silhouetteOfMesh(view.mesh);
			view.silhouette.stripLines = stripLinesOf(
				*def.parts, stripLayout(def, turnQuanta, config), turnRadiansFromQuanta(turnQuanta), config, view.silhouette.silhouette.rings
			);
		}
		return view;
	}

} // namespace assets
