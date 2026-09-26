// Pond and channel rings for TerrainPolygonBuilder
// (docs/technical/organic-terrain/terrain-polygons-architecture.md D7, D8):
// pond rims, river chains and their Catmull-Rom centerlines, mouths into lakes
// and ponds, bank offsets, the fordable split, stroking, and thalwegs.

#include "world/chunk/ChunkSampleResult.h"
#include "world/chunk/TerrainPolygonBuilderDetail.h"

#include <contour/CatmullRom.h>
#include <contour/ClipRing.h>
#include <contour/RingPins.h>
#include <contour/Stroke.h>
#include <core/Vec2d.h>
#include <core/Vec2i64.h>
#include <math/DeterministicMath.h>
#include <polygon/Polygon.h>
#include <predicates/Predicates.h>
#include <utils/Log.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <numbers>
#include <optional>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

namespace engine::world::terrain_detail {

	namespace {

		// The longest river sub-segment the gather emits (RiverNetwork2D's trunk
		// step). A centerline sample's Catmull-Rom span reads the nodes one step
		// either side of it, so every sample that matters must lie a full step
		// inside the gather: the kept samples, and the samples a relevant one's
		// widthRatio window reads, which reach kThalwegHalfWindowMaxM past it.
		constexpr double kGatherStepM = 20.0;
		static_assert(
			Builder::kChannelKeepReachM + kGatherStepM <= kRiverGatherMarginM &&
				Builder::kChannelKeepReachM - Builder::kChannelDecisionReachM + Builder::kThalwegHalfWindowMaxM + kGatherStepM <=
					kRiverGatherMarginM,
			"every kept centerline sample and every widthRatio window must lie a gather step inside the gather"
		);

		// The channel reach base (Builder, "Channel reach"), grown to the apron of a
		// build with a wider one.
		double channelReachBaseM(const Region& region) {
			return std::max(Builder::kChannelReachBaseM, static_cast<double>(region.apronTiles) + 2.0 * Builder::kChannelReachSlackM);
		}

		// Clip a pond or channel ring to the extended region, then the shared tail:
		// pinned on the world lattice, on the extended boundary lines, and at
		// `extraPins` (the fordable cut vertices). The extended boundary is pinned
		// so resampling cannot chamfer the corner where a bank meets its closure,
		// leaving an unflagged sliver of closure inside the region.
		std::vector<Ring> finishVectorRing(const Ring& ring, std::span<const Vec2i64> extraPins, const Region& region,
										   ChunkCoordinate coord, const char* what) {
			std::vector<Ring> out;
			if (!geometry::isSimple(ring).pass) {
				// The radius clamp keeps the inner bank under the local radius, which
				// is enough while the half-width stays under the bend's tightest
				// radius. Past that, or where the centerline comes back within its
				// own width, the banks cross beyond any one sample's reach.
				LOG_WARNING(World, "Chunk (%d, %d): dropped a self-overlapping %s (%zu vertices)", coord.x, coord.y, what, ring.size());
				return out;
			}
			const std::array<int64_t, 2> xLines = {region.extMin.x, region.extMax.x};
			const std::array<int64_t, 2> yLines = {region.extMin.y, region.extMax.y};
			for (Ring& piece : geometry::clipRingToRect(ring, region.extendedRect())) {
				std::optional<Ring> finished = pinResampleSimplifyValidate(std::move(piece), xLines, yLines, extraPins, coord, what);
				if (finished && !areaBelowFloor(*finished)) {
					out.push_back(std::move(*finished));
				}
			}
			return out;
		}

		TerrainRing vectorTerrainRing(Ring ring, TerrainRingKind kind, WaterKind water, std::span<const Vec2i64> cutVertices,
									  const ExtendedGrid& grid, const Region& region) {
			TerrainRing terrain;
			terrain.profiles = shoreProfiles(ring, water, grid, SideRule::VectorWater);
			for (size_t i = 0; i < ring.size(); ++i) {
				if (isOnExtendedSide(ring[i], ring[(i + 1) % ring.size()], region)) {
					terrain.profiles[i].flags |= ShoreProfile::kFlagSynthetic;
				}
				if (std::find(cutVertices.begin(), cutVertices.end(), ring[i]) != cutVertices.end()) {
					terrain.profiles[i].flags |= ShoreProfile::kFlagFordableCut;
				}
			}
			terrain.ring		   = std::move(ring);
			terrain.kind		   = kind;
			terrain.water		   = water;
			terrain.blocksMovement = true;
			terrain.holeCapable	   = false;
			return terrain;
		}

	} // namespace

	// Each vertex's radius is perturbed by world-space noise read at the
	// unperturbed rim point. A radial perturbation of a star-shaped rim stays simple.
	Ring pondRim(const Builder::Pond& pond, uint64_t worldSeed) {
		const uint32_t	 seed		   = purposeSeed(worldSeed, kSaltPondRim);
		constexpr double kTwoPi		   = 2.0 * std::numbers::pi;
		const double	 circumference = kTwoPi * static_cast<double>(pond.radius);
		const auto		 count		   = std::max<size_t>(8, static_cast<size_t>(std::ceil(circumference / Builder::kPondRimSpacingM)));
		const double	 noiseAmpM	   = static_cast<double>(Builder::kPondRimNoiseAmpMm) / kMmPerMeter;
		Ring			 ring(count);
		for (size_t k = 0; k < count; ++k) {
			const double theta	= kTwoPi * static_cast<double>(k) / static_cast<double>(count);
			const double radius = worldgen::PondNetwork2D::rimRadiusAt(pond, theta);
			const double c		= foundation::det_math::cos(theta);
			const double s		= foundation::det_math::sin(theta);
			const Vec2d	 rim{pond.cx + radius * c, pond.cy + radius * s};
			const double noise = noiseAmpM * static_cast<double>(worldNoise(
												 toMm(rim), Builder::kBankNoiseWavelengthM, seed, Builder::kBankNoiseOctaves
											 ));
			ring[k] = toMm({pond.cx + (radius + noise) * c, pond.cy + (radius + noise) * s});
		}
		return ring;
	}

	namespace {

		// ---- Chains: gathered segments joined end to end (D7 step 1) ----

		// sIn/sOut: the arc coordinate of the segment arriving at and leaving the
		// node (they differ where a coarse tile joint resets it).
		struct ChainNode {
			Vec2i64 mm;
			double	halfWidthM = 0.0;
			double	sIn		   = 0.0;
			double	sOut	   = 0.0;
		};
		using Chain = std::vector<ChainNode>;

		// Segments joined by exact endpoint match after quantizing to the mm, which
		// absorbs the float noise between a coarse-tile pair's last point and the
		// next pair's first. A node that is not one-in, one-out ends every chain
		// through it. A node's position is its mm key, so every chunk that has the
		// node uses the same point; its half-width is the incoming segment's (the
		// outgoing one's at a chain start), which every chunk that has the node as
		// a chain interior agrees on.
		std::vector<Chain> joinChains(std::span<const Builder::RiverSegment> segments) {
			struct Edge {
				Vec2i64 a;
				Vec2i64 b;
				double	hw0 = 0.0;
				double	hw1 = 0.0;
				double	s0	= 0.0;
				double	s1	= 0.0;
			};
			std::vector<Edge> edges;
			edges.reserve(segments.size());
			for (const Builder::RiverSegment& s : segments) {
				const Vec2i64 a = toMm({s.x0, s.y0});
				const Vec2i64 b = toMm({s.x1, s.y1});
				if (a != b) {
					edges.push_back({a, b, static_cast<double>(s.halfWidth0), static_cast<double>(s.halfWidth1), s.s0, s.s1});
				}
			}
			std::sort(edges.begin(), edges.end(), [](const Edge& l, const Edge& r) {
				return std::tie(l.a, l.b, l.hw0, l.hw1, l.s0, l.s1) < std::tie(r.a, r.b, r.hw0, r.hw1, r.s0, r.s1);
			});
			edges.erase(
				std::unique(edges.begin(), edges.end(), [](const Edge& l, const Edge& r) { return l.a == r.a && l.b == r.b; }),
				edges.end()
			);

			struct Node {
				int	   in	   = 0;
				int	   out	   = 0;
				size_t outEdge = 0;
			};
			std::map<Vec2i64, Node> nodes;
			for (size_t i = 0; i < edges.size(); ++i) {
				Node& from = nodes[edges[i].a];
				++from.out;
				from.outEdge = i;
				++nodes[edges[i].b].in;
			}
			auto through = [&nodes](const Vec2i64& v) {
				const Node& node = nodes.at(v);
				return node.in == 1 && node.out == 1;
			};

			std::vector<Chain>	 chains;
			std::vector<uint8_t> used(edges.size(), 0);
			auto				 walk = [&](size_t first) {
				Chain  chain{{edges[first].a, edges[first].hw0, edges[first].s0, edges[first].s0}};
				size_t e = first;
				while (true) {
					used[e]			  = 1;
					chain.back().sOut = edges[e].s0;
					chain.push_back({edges[e].b, edges[e].hw1, edges[e].s1, edges[e].s1});
					if (!through(edges[e].b)) {
						break;
					}
					const size_t next = nodes.at(edges[e].b).outEdge;
					if (used[next] != 0) {
						break; // a closed loop of through nodes
					}
					e = next;
				}
				chains.push_back(std::move(chain));
			};
			for (size_t i = 0; i < edges.size(); ++i) {
				if (!through(edges[i].a)) {
					walk(i);
				}
			}
			for (size_t i = 0; i < edges.size(); ++i) {
				if (used[i] == 0) {
					walk(i);
				}
			}
			return chains;
		}

		// Drops nodes that sit closer than kMinChainSpanM to their predecessor (the
		// original one, so the choice is per node pair, not a running state). At the
		// chain's end the node before the last goes instead, keeping the end point.
		// Returns false when the chain collapses to a stub.
		bool dropShortSpans(Chain& chain) {
			static constexpr double kMinMm = Builder::kMinChainSpanM * kMmPerMeter;
			auto					shortSpan = [&chain](size_t i) {
				   const double dx = static_cast<double>(chain[i].mm.x - chain[i - 1].mm.x);
				   const double dy = static_cast<double>(chain[i].mm.y - chain[i - 1].mm.y);
				   return std::sqrt(dx * dx + dy * dy) < kMinMm;
			};
			const size_t		 n = chain.size();
			std::vector<uint8_t> drop(n, 0);
			for (size_t i = 1; i + 1 < n; ++i) {
				drop[i] = shortSpan(i) ? 1 : 0;
			}
			if (n > 2 && shortSpan(n - 1)) {
				drop[n - 2] = 1;
			}
			if (n == 2 && shortSpan(1)) {
				return false;
			}
			Chain kept;
			kept.reserve(n);
			for (size_t i = 0; i < n; ++i) {
				if (drop[i] == 0) {
					kept.push_back(chain[i]);
				}
			}
			chain = std::move(kept);
			return chain.size() >= 2;
		}

		// ---- Reaches: the stretches of a chain's centerline that become ribbons ----

		enum class Ground : uint8_t { Land, Water };

		// The receiving bodies for the mouth test, as functions of world position
		// alone so every chunk classifies a centerline sample alike however far
		// from it the sample lies: the waterline field (D5, D6) and the unclipped
		// pond rims (D8). Both sit within a few cm of the rings built from them.
		class ReceivingWater {
		  public:
			ReceivingWater(const WaterlineField& waterline, std::span<const Builder::Pond> ponds, uint64_t worldSeed)
				: m_waterline(waterline) {
				for (const Builder::Pond& pond : ponds) {
					Rim rim{pondRim(pond, worldSeed), {}, {}};
					rim.lo = rim.hi = rim.ring.front();
					for (const Vec2i64& v : rim.ring) {
						rim.lo = {std::min(rim.lo.x, v.x), std::min(rim.lo.y, v.y)};
						rim.hi = {std::max(rim.hi.x, v.x), std::max(rim.hi.y, v.y)};
					}
					m_rims.push_back(std::move(rim));
				}
			}

			[[nodiscard]] Ground at(const Vec2i64& p) const {
				for (const Rim& rim : m_rims) {
					if (p.x >= rim.lo.x && p.x <= rim.hi.x && p.y >= rim.lo.y && p.y <= rim.hi.y &&
						geometry::pointInPolygon(p, rim.ring) != geometry::PointInPolygon::Outside) {
						return Ground::Water;
					}
				}
				return m_waterline.waterAt(p) ? Ground::Water : Ground::Land;
			}

		  private:
			struct Rim {
				Ring	ring;
				Vec2i64 lo;
				Vec2i64 hi;
			};
			const WaterlineField& m_waterline;
			std::vector<Rim>	  m_rims;
		};

		struct ReachPoint {
			Vec2d  position;			  // world meters
			double rawHalfWidthM = 0.0;	  // the segment half-width, which fordability reads
			double flare		 = 1.0;	  // mouth widening, a factor on the half-width
			double asymmetry	 = 1.0;	  // share of the bend asymmetry kept, faded out over a flare
			float  widthRatio	 = 1.0F;  // ThalwegPath::widthRatio
			double arcLengthM	 = 0.0;	  // ThalwegPath::arcLengthM
		};

		struct Reach {
			std::vector<ReachPoint> points;
			// Ends where the chain was trimmed to the kept samples: a butt cap outside
			// the extended region, never seen.
			bool trimmedStart = false;
			bool trimmedEnd	  = false;
		};

		// hw / mean hw over the samples within half a window each way (arc length,
		// summed outward from the sample so it never depends on the chain's start;
		// each half capped at kThalwegHalfWindowMaxM so the gather covers it). Raw
		// segment widths: the riffle/pool modulation is the signal (R4); a mouth
		// flare is not a riffle.
		std::vector<float> widthRatios(const std::vector<geometry::CenterlineSample>& samples) {
			const size_t	   n = samples.size();
			std::vector<float> out(n, 1.0F);
			for (size_t i = 0; i < n; ++i) {
				const double half =
					std::min(0.5 * Builder::kThalwegWindowW * 2.0 * samples[i].halfWidthM, Builder::kThalwegHalfWindowMaxM);
				double		 sum   = samples[i].halfWidthM;
				int			 count = 1;
				double		 d	   = 0.0;
				for (size_t k = i; k > 0; --k) {
					d += geometry::length(samples[k].position - samples[k - 1].position);
					if (d > half) {
						break;
					}
					sum += samples[k - 1].halfWidthM;
					++count;
				}
				d = 0.0;
				for (size_t k = i; k + 1 < n; ++k) {
					d += geometry::length(samples[k + 1].position - samples[k].position);
					if (d > half) {
						break;
					}
					sum += samples[k + 1].halfWidthM;
					++count;
				}
				const double mean = sum / static_cast<double>(count);
				out[i]			  = mean > 0.0 ? static_cast<float>(samples[i].halfWidthM / mean) : 1.0F;
			}
			return out;
		}

		// The flare and extension lengths at a mouth whose channel is w wide (D7),
		// capped so a mouth's reach along the river stays bounded.
		double flareLengthM(double w) {
			return std::min(Builder::kMouthFlareW * w, Builder::kMouthFlareMaxM);
		}

		double extendLengthM(double w) {
			return std::min(Builder::kMouthExtendW * w, Builder::kMouthExtendMaxM);
		}

		// One run of kept samples [first, last] of a chain into reaches, handling
		// river mouths (D7). Where the centerline passes from Land into Water (an
		// inflow mouth at s_m), the channel flares over [s_m - 2 w, s_m] and runs on
		// 1 w into the water body, then stops (both lengths capped); where it passes
		// from Water onto Land (a lake outlet), it starts 1 w inside the water. Water
		// with no Land transition in view is dropped: it lies inside the receiving
		// body. Every decision is a function of the ground within
		// kChannelDecisionReachM of a sample, and the ground is a function of world
		// position, so every chunk that keeps that much river around a sample
		// decides alike for it.
		void splitRun(const std::vector<geometry::CenterlineSample>& samples, const std::vector<float>& ratios,
					  const std::vector<double>& arcs, const std::vector<Ground>& ground, size_t first, size_t last,
					  std::vector<Reach>& out) {
			const size_t		 count = last - first + 1;
			std::vector<uint8_t> include(count, 1);
			std::vector<double>	 flare(count, 1.0);
			std::vector<double>	 asymmetry(count, 1.0);
			auto				 pos = [&samples](size_t i) { return samples[i].position; };
			auto				 hw	 = [&samples](size_t i) { return samples[i].halfWidthM; };

			std::optional<double> extensionM; // past the chain's true end, into the water body

			for (size_t a = first; a <= last;) {
				if (ground[a] != Ground::Water) {
					++a;
					continue;
				}
				size_t b = a;
				while (b < last && ground[b + 1] == Ground::Water) {
					++b;
				}
				for (size_t k = a; k <= b; ++k) {
					include[k - first] = 0;
				}

				const bool inflow  = a > first && ground[a - 1] == Ground::Land;
				const bool outflow = b < last && ground[b + 1] == Ground::Land;
				size_t	   inEnd   = a;
				size_t	   outFrom = b;
				if (inflow) {
					const double w		 = 2.0 * hw(a);
					const double extendM = extendLengthM(w);
					double		 d		 = 0.0;
					include[a - first]	 = 1;
					while (inEnd < b) {
						const double step = geometry::length(pos(inEnd + 1) - pos(inEnd));
						if (d + step > extendM) {
							break;
						}
						d += step;
						++inEnd;
						include[inEnd - first] = 1;
					}
					if (inEnd == b && b == samples.size() - 1 && d < extendM) {
						extensionM = extendM - d;
					}
					for (size_t k = a; k <= inEnd; ++k) {
						flare[k - first]	 = Builder::kMouthFlare;
						asymmetry[k - first] = 0.0;
					}
					const double flareM = flareLengthM(w);
					double		 up		= 0.0;
					for (size_t k = a; k > first; --k) {
						up += geometry::length(pos(k) - pos(k - 1));
						if (up >= flareM) {
							break;
						}
						const double ramp	 = smoothstep(1.0 - up / flareM);
						const size_t idx	 = k - 1 - first;
						flare[idx]			 = std::max(flare[idx], 1.0 + (Builder::kMouthFlare - 1.0) * ramp);
						asymmetry[idx]		 = std::min(asymmetry[idx], 1.0 - ramp);
					}
				}
				if (outflow) {
					const double extendM = extendLengthM(2.0 * hw(b));
					double		 d		 = 0.0;
					include[b - first]	 = 1;
					while (outFrom > a) {
						const double step = geometry::length(pos(outFrom) - pos(outFrom - 1));
						if (d + step > extendM) {
							break;
						}
						d += step;
						--outFrom;
						include[outFrom - first] = 1;
					}
				}
				// A crossing narrower than the two overlaps is kept whole.
				if (inflow && outflow && outFrom <= inEnd + 1) {
					for (size_t k = a; k <= b; ++k) {
						include[k - first] = 1;
					}
				}
				a = b + 1;
			}

			for (size_t i = first; i <= last;) {
				if (include[i - first] == 0) {
					++i;
					continue;
				}
				Reach reach;
				reach.trimmedStart = i == first && first > 0;
				size_t j		   = i;
				while (j <= last && include[j - first] != 0) {
					reach.points.push_back({pos(j), hw(j), flare[j - first], asymmetry[j - first], ratios[j], arcs[j]});
					++j;
				}
				const size_t end = j - 1;
				reach.trimmedEnd = end == last && last + 1 < samples.size();
				if (extensionM && end == samples.size() - 1 && end > 0) {
					const Vec2d	 chord = pos(end) - pos(end - 1);
					const Vec2d	 dir   = chord * (1.0 / geometry::length(chord));
					const auto	 steps = std::max<size_t>(1, static_cast<size_t>(std::ceil(*extensionM / Builder::kCenterlineSpacingM)));
					for (size_t q = 1; q <= steps; ++q) {
						const double along = *extensionM * static_cast<double>(q) / static_cast<double>(steps);
						reach.points.push_back({pos(end) + dir * along, hw(end), Builder::kMouthFlare, 0.0, 1.0F, arcs[end] + along});
					}
				}
				if (reach.points.size() >= 2) {
					out.push_back(std::move(reach));
				}
				i = j;
			}
		}

		// The samples a chunk keeps (Builder, "Channel reach"): every relevant sample
		// and every sample within kChannelDecisionReachM of one along the
		// centerline, one pass each way.
		std::vector<uint8_t> keptSamples(const std::vector<geometry::CenterlineSample>& samples, const Region& region) {
			const size_t		 n = samples.size();
			const double		 base = channelReachBaseM(region);
			std::vector<uint8_t> kept(n, 0);
			std::vector<uint8_t> relevant(n, 0);
			for (size_t i = 0; i < n; ++i) {
				const double outsideM = static_cast<double>(region.outsideChunkMm(toMm(samples[i].position))) / kMmPerMeter;
				relevant[i]			  = outsideM <= base + Builder::kChannelReachHalfWidths * samples[i].halfWidthM ? 1 : 0;
			}
			auto sweep = [&](bool forward) {
				std::optional<double> since; // arc length back to the last relevant sample
				for (size_t k = 0; k < n; ++k) {
					const size_t i = forward ? k : n - 1 - k;
					if (since && k > 0) {
						const size_t prev = forward ? i - 1 : i + 1;
						*since += geometry::length(samples[i].position - samples[prev].position);
					}
					if (relevant[i] != 0) {
						since = 0.0;
					}
					if (since && *since <= Builder::kChannelDecisionReachM) {
						kept[i] = 1;
					}
				}
			};
			sweep(true);
			sweep(false);
			return kept;
		}

		std::vector<Reach> extractReaches(const std::vector<geometry::CenterlineSample>& samples, const std::vector<double>& arcs,
										  const Region& region, const ReceivingWater& water) {
			const size_t			   n	= samples.size();
			const std::vector<uint8_t> kept = keptSamples(samples, region);
			std::vector<Ground>		   ground(n, Ground::Land);
			for (size_t i = 0; i < n; ++i) {
				if (kept[i] != 0) {
					ground[i] = water.at(toMm(samples[i].position));
				}
			}
			const std::vector<float> ratios = widthRatios(samples);
			std::vector<Reach>		 reaches;
			for (size_t i = 0; i < n;) {
				if (kept[i] == 0) {
					++i;
					continue;
				}
				size_t j = i;
				while (j + 1 < n && kept[j + 1] != 0) {
					++j;
				}
				splitRun(samples, ratios, arcs, ground, i, j, reaches);
				i = j + 1;
			}
			return reaches;
		}

		// ---- Ribbons: bank offsets, the fordable split, the stroke ----

		struct ChannelSeeds {
			uint32_t leftFine;
			uint32_t leftLow;
			uint32_t rightFine;
			uint32_t rightLow;
		};

		// One centerline point with everything the stroke and the thalweg need.
		struct RibbonPoint {
			Vec2d  position;
			double rawHalfWidthM = 0.0;
			double halfWidthM	 = 0.0; // bankfull, flared
			double kappa		 = 0.0; // signed, 1/m, positive turning left
			double radiusM		 = std::numeric_limits<double>::infinity();
			double asymmetryM	 = 0.0; // a: the outer bank's extra offset
			double leftM		 = 0.0;
			double rightM		 = 0.0;
			float  widthRatio	 = 1.0F;
			double arcLengthM	 = 0.0;
			bool   cut			 = false; // a fordability cut, shared by the pieces either side
		};

		bool blocks(double rawHalfWidthM) {
			return 2.0 * rawHalfWidthM >= Builder::kFordableWidthM;
		}

		// D7 steps 3-4 at one point: bankfull half-width, bend asymmetry, bank noise
		// at the point's world position, then the inner bank clamped under the local
		// radius of curvature.
		void bankOffsets(RibbonPoint& p, double flare, double asymmetryShare, const ChannelSeeds& seeds) {
			const double hw	   = p.rawHalfWidthM * flare;
			const double kAbs  = std::abs(p.kappa);
			const double a	   = std::min(Builder::kAsymmetryMaxFrac, Builder::kAsymmetryCurvatureGain * kAbs * hw) * hw * asymmetryShare;
			p.halfWidthM	   = hw;
			p.asymmetryM	   = a;
			const double outer = hw + a;
			const double inner = hw + Builder::kInnerBankAsymmetryShare * a;
			// Positive curvature turns left: the left bank is on the inside.
			double left	 = p.kappa > 0.0 ? inner : outer;
			double right = p.kappa > 0.0 ? outer : inner;

			const Vec2i64 mm	 = toMm(p.position);
			const double  turn	 = kAbs * Builder::kChannelBankNoiseWavelengthM;
			const double  damp	 = std::max(Builder::kNoiseDampFloor, 1.0 - turn / Builder::kNoiseDampTurnRad);
			auto		  noise	 = [&mm, hw](uint32_t fineSeed, uint32_t lowSeed) {
				 const double fine = static_cast<double>(worldNoise(
					 mm, Builder::kChannelBankNoiseWavelengthM, fineSeed, Builder::kChannelBankNoiseOctaves
				 ));
				 const double low = static_cast<double>(worldNoise(
					 mm, Builder::kChannelLowNoiseWavelengthM, lowSeed, Builder::kChannelLowNoiseOctaves
				 ));
				 return (Builder::kChannelBankNoiseFrac * fine + Builder::kChannelLowNoiseFrac * low) * hw;
			};
			left += damp * noise(seeds.leftFine, seeds.leftLow);
			right += damp * noise(seeds.rightFine, seeds.rightLow);

			const double limit = Builder::kRadiusClampFrac * p.radiusM;
			if (p.kappa > 0.0) {
				left = std::min(left, limit);
			} else if (p.kappa < 0.0) {
				right = std::min(right, limit);
			}
			p.leftM	 = std::max(0.0, left);
			p.rightM = std::max(0.0, right);
		}

		// The reach's centerline with the fordable cuts in place and every offset
		// computed once, so the two pieces either side of a cut read the same
		// numbers for it.
		std::vector<RibbonPoint> ribbonPoints(const Reach& reach, const ChannelSeeds& seeds) {
			const std::vector<ReachPoint>& pts = reach.points;
			const size_t				   n   = pts.size();

			std::vector<double> kappa(n, 0.0);
			std::vector<double> radius(n, std::numeric_limits<double>::infinity());
			for (size_t k = 1; k + 1 < n; ++k) {
				const double r = geometry::localRadiusOfCurvatureM(pts[k - 1].position, pts[k].position, pts[k + 1].position);
				if (!std::isfinite(r) || r == 0.0) {
					continue;
				}
				const Vec2d	 a	   = pts[k].position - pts[k - 1].position;
				const Vec2d	 b	   = pts[k + 1].position - pts[k].position;
				const double cross = a.x * b.y - a.y * b.x;
				kappa[k]		   = (cross > 0.0 ? 1.0 : -1.0) / r;
				radius[k]		   = r;
			}

			std::vector<RibbonPoint> out;
			out.reserve(n + 4);
			auto push = [&](size_t k) {
				RibbonPoint p;
				p.position		= pts[k].position;
				p.rawHalfWidthM = pts[k].rawHalfWidthM;
				p.kappa			= kappa[k];
				p.radiusM		= radius[k];
				p.widthRatio	= pts[k].widthRatio;
				p.arcLengthM	= pts[k].arcLengthM;
				bankOffsets(p, pts[k].flare, pts[k].asymmetry, seeds);
				out.push_back(p);
			};

			static constexpr double kFordHalfM = Builder::kFordableWidthM / 2.0;
			bool					cutNext	   = false;
			for (size_t k = 0; k < n; ++k) {
				push(k);
				out.back().cut = cutNext;
				cutNext		   = false;
				if (k + 1 == n || blocks(pts[k].rawHalfWidthM) == blocks(pts[k + 1].rawHalfWidthM)) {
					continue;
				}
				// Half-width is linear in the span parameter between two samples of one
				// span, so the crossing is linear between them too (D7 step 5).
				const double h0 = pts[k].rawHalfWidthM;
				const double h1 = pts[k + 1].rawHalfWidthM;
				const double u	= (kFordHalfM - h0) / (h1 - h0);
				if (u <= 0.0) {
					out.back().cut = true;
					continue;
				}
				if (u >= 1.0) {
					cutNext = true;
					continue;
				}
				RibbonPoint p;
				p.position = pts[k].position + (pts[k + 1].position - pts[k].position) * u;
				if (p.position == pts[k].position || p.position == pts[k + 1].position) {
					(p.position == pts[k].position ? out.back().cut : cutNext) = true;
					continue;
				}
				p.rawHalfWidthM = kFordHalfM;
				p.kappa			= lerp(kappa[k], kappa[k + 1], u);
				p.radiusM		= p.kappa != 0.0 ? 1.0 / std::abs(p.kappa) : std::numeric_limits<double>::infinity();
				p.widthRatio	= static_cast<float>(lerp(pts[k].widthRatio, pts[k + 1].widthRatio, u));
				p.arcLengthM	= lerp(pts[k].arcLengthM, pts[k + 1].arcLengthM, u);
				p.cut			= true;
				bankOffsets(p, lerp(pts[k].flare, pts[k + 1].flare, u), lerp(pts[k].asymmetry, pts[k + 1].asymmetry, u), seeds);
				out.push_back(p);
			}

			// A cut at a reach end, or one whose two sides block alike (the width
			// touched the threshold at a single sample), splits nothing.
			const size_t m = out.size();
			for (size_t i = 0; i < m; ++i) {
				if (!out[i].cut) {
					continue;
				}
				if (i == 0 || i + 1 == m || blocks(out[i - 1].rawHalfWidthM) == blocks(out[i + 1].rawHalfWidthM)) {
					out[i].cut = false;
				}
			}
			return out;
		}

		// Both bank points strokePolyline places at a cut offset along `normal`.
		std::array<Vec2i64, 2> cutVertices(const RibbonPoint& p, const Vec2d& normal) {
			return {geometry::strokeBankPoint(p.position, normal, -p.rightM), geometry::strokeBankPoint(p.position, normal, p.leftM)};
		}

		struct ChannelPiece {
			Ring				 ring;
			std::vector<Vec2i64> cuts; // this piece's fordable cut vertices
			bool				 blocksMovement = true;
			float				 meanHalfWidthM = 0.0F;
		};

		// Every non-cut point of a piece lies on one side of the threshold, and a
		// piece always has one (cuts are never adjacent, nor at a reach end).
		bool pieceBlocks(const std::vector<RibbonPoint>& pts, size_t s, size_t e) {
			for (size_t i = s; i <= e; ++i) {
				if (!pts[i].cut) {
					return blocks(pts[i].rawHalfWidthM);
				}
			}
			return true;
		}

		std::vector<ChannelPiece> strokeReach(const Reach& reach, const std::vector<RibbonPoint>& pts) {
			std::vector<Vec2d> centerline(pts.size());
			for (size_t i = 0; i < pts.size(); ++i) {
				centerline[i] = pts[i].position;
			}
			std::vector<size_t> bounds = {0};
			for (size_t i = 1; i + 1 < pts.size(); ++i) {
				if (pts[i].cut) {
					bounds.push_back(i);
				}
			}
			bounds.push_back(pts.size() - 1);

			std::vector<ChannelPiece> pieces;
			for (size_t b = 0; b + 1 < bounds.size(); ++b) {
				const size_t s = bounds[b];
				const size_t e = bounds[b + 1];

				std::vector<double> left(e - s + 1);
				std::vector<double> right(e - s + 1);
				double				hwSum = 0.0;
				for (size_t i = s; i <= e; ++i) {
					left[i - s]	 = pts[i].leftM;
					right[i - s] = pts[i].rightM;
					hwSum += pts[i].halfWidthM;
				}

				ChannelPiece piece;
				piece.blocksMovement = pieceBlocks(pts, s, e);
				piece.meanHalfWidthM = static_cast<float>(hwSum / static_cast<double>(e - s + 1));

				geometry::StrokeArgs args;
				args.centerline	  = std::span<const Vec2d>(centerline.data() + s, e - s + 1);
				args.leftOffsetM  = left;
				args.rightOffsetM = right;
				args.capSpacingM  = Builder::kCapSpacingM;
				if (pts[s].cut) {
					args.startCap	 = geometry::StrokeCap::Butt;
					args.startNormal = geometry::strokeNormal(centerline, s);
					const auto v	 = cutVertices(pts[s], *args.startNormal);
					piece.cuts.insert(piece.cuts.end(), v.begin(), v.end());
				} else {
					args.startCap = s == 0 && reach.trimmedStart ? geometry::StrokeCap::Butt : geometry::StrokeCap::Round;
				}
				if (pts[e].cut) {
					args.endCap	   = geometry::StrokeCap::Butt;
					args.endNormal = geometry::strokeNormal(centerline, e);
					const auto v   = cutVertices(pts[e], *args.endNormal);
					piece.cuts.insert(piece.cuts.end(), v.begin(), v.end());
				} else {
					args.endCap = e + 1 == pts.size() && reach.trimmedEnd ? geometry::StrokeCap::Butt : geometry::StrokeCap::Round;
				}
				piece.ring = geometry::strokePolyline(args);
				pieces.push_back(std::move(piece));
			}
			return pieces;
		}

		// D2 thalweg: the centerline offset toward the outer bank by the asymmetry,
		// one path per stretch a bake texel can read. A texel reads a point up to
		// kThalwegReachHalfWidths bankfull half-widths from it, and the point lies up
		// to the asymmetry off its centerline sample: at most kChannelReachHalfWidths
		// raw half-widths from the sample, which is kept when that plus the slack
		// (which keeps a point's neighbors too, however its flare ramps, so every
		// segment near a texel is whole) reaches the bake region. Every such sample
		// is relevant, so a texel inside two chunks' bake regions reads the same
		// points from both.
		void appendThalwegs(const std::vector<RibbonPoint>& pts, const Region& region, std::vector<ThalwegPath>& out) {
			std::vector<Vec2d> centerline(pts.size());
			for (size_t i = 0; i < pts.size(); ++i) {
				centerline[i] = pts[i].position;
			}
			ThalwegPath path;
			auto		flush = [&out, &path]() {
				   if (path.points.size() >= 2) {
					   out.push_back(std::move(path));
				   }
				   path = {};
			};
			for (size_t i = 0; i < pts.size(); ++i) {
				const RibbonPoint& p	   = pts[i];
				const double	   towards = p.kappa > 0.0 ? -p.asymmetryM : (p.kappa < 0.0 ? p.asymmetryM : 0.0);
				const Vec2i64	   mm	   = toMm(p.position + geometry::strokeNormal(centerline, i) * towards);
				const double	   reachM =
					Builder::kBakeMarginM + Builder::kChannelReachHalfWidths * p.rawHalfWidthM + Builder::kChannelReachSlackM;
				if (static_cast<double>(region.outsideChunkMm(toMm(p.position))) / kMmPerMeter > reachM) {
					flush();
					continue;
				}
				path.points.push_back(mm);
				path.halfWidthM.push_back(static_cast<float>(p.halfWidthM));
				path.widthRatio.push_back(p.widthRatio);
				path.curvature.push_back(static_cast<float>(p.kappa));
				path.arcLengthM.push_back(p.arcLengthM);
			}
			flush();
		}

	} // namespace

	void buildPonds(std::vector<TerrainRing>& rings, std::span<const Builder::Pond> ponds, const ExtendedGrid& grid,
					const Region& region, uint64_t worldSeed, ChunkCoordinate coord) {
		for (const Builder::Pond& pond : ponds) {
			for (Ring& ring : finishVectorRing(pondRim(pond, worldSeed), {}, region, coord, "pond ring")) {
				rings.push_back(vectorTerrainRing(std::move(ring), TerrainRingKind::Pond, WaterKind::Pond, {}, grid, region));
			}
		}
	}

	void buildChannels(ChunkTerrainPolygons& out, std::span<const Builder::RiverSegment> segments,
					   std::span<const Builder::Pond> ponds, const WaterlineField& waterline, const ExtendedGrid& grid,
					   const Region& region, uint64_t worldSeed, ChunkCoordinate coord) {
		if (segments.empty()) {
			return;
		}
		const ChannelSeeds seeds{
			purposeSeed(worldSeed, kSaltLeftBankFine), purposeSeed(worldSeed, kSaltLeftBankLow),
			purposeSeed(worldSeed, kSaltRightBankFine), purposeSeed(worldSeed, kSaltRightBankLow)
		};
		const ReceivingWater	 water(waterline, ponds, worldSeed);
		std::vector<TerrainRing> channels;

		for (Chain& chain : joinChains(segments)) {
			if (!dropShortSpans(chain)) {
				continue;
			}
			std::vector<Vec2d>	points(chain.size());
			std::vector<double> halfWidths(chain.size());
			for (size_t i = 0; i < chain.size(); ++i) {
				points[i]	  = toMeters(chain[i].mm);
				halfWidths[i] = chain[i].halfWidthM;
			}
			const std::vector<geometry::CenterlineSample> samples =
				geometry::sampleCatmullRom(points, halfWidths, Builder::kCenterlineSpacingM);
			// Arc coordinate linear within each span, from the segment leaving its
			// first node to the one arriving at its last.
			std::vector<double> arcs(samples.size());
			for (size_t i = 0; i < samples.size(); ++i) {
				const geometry::CenterlineSample& s = samples[i];
				arcs[i] = lerp(chain[s.segment].sOut, chain[s.segment + 1].sIn, s.t);
			}

			for (const Reach& reach : extractReaches(samples, arcs, region, water)) {
				const std::vector<RibbonPoint> ribbon = ribbonPoints(reach, seeds);
				appendThalwegs(ribbon, region, out.thalwegs);
				for (ChannelPiece& piece : strokeReach(reach, ribbon)) {
					for (Ring& ring : finishVectorRing(piece.ring, piece.cuts, region, coord, "channel ring")) {
						TerrainRing terrain = vectorTerrainRing(
							std::move(ring), TerrainRingKind::Channel, WaterKind::River, piece.cuts, grid, region
						);
						terrain.blocksMovement = piece.blocksMovement;
						terrain.meanHalfWidthM = piece.meanHalfWidthM;
						channels.push_back(std::move(terrain));
					}
				}
			}
		}
		for (TerrainRing& channel : channels) {
			out.rings.push_back(std::move(channel));
		}
	}

} // namespace engine::world::terrain_detail
