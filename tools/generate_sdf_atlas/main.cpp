// SDF Atlas Generator Tool
// Generates multi-channel signed distance field atlases for fonts

#include <msdfgen.h>
#include <msdfgen-ext.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>

struct GlyphData {
	char character;
	msdfgen::unicode_t unicode;
	double advance;
	msdfgen::Shape shape;

	// Atlas cell coordinates (in pixels) - the full allocated cell
	int atlasX = 0;
	int atlasY = 0;
	int atlasWidth = 0;
	int atlasHeight = 0;

	// Atlas bounds (in pixels) - actual glyph content within the cell
	// Reference: https://github.com/Chlumsky/msdf-atlas-gen/issues/2
	// atlasBounds defines where the actual rendered glyph is within the cell
	double atlasBoundsLeft = 0;
	double atlasBoundsBottom = 0;
	double atlasBoundsRight = 0;
	double atlasBoundsTop = 0;

	// Plane bounds (in EM units) - glyph positioning relative to baseline
	// Reference: https://github.com/Chlumsky/msdf-atlas-gen/discussions/17
	double planeLeft = 0;
	double planeBottom = 0;
	double planeRight = 0;
	double planeTop = 0;
};

struct AtlasConfig {
	const char* fontPath = "fonts/Roboto-Regular.ttf";
	const char* outputPath = "fonts/Roboto-SDF.png";
	const char* metadataPath = "fonts/Roboto-SDF.json";

	int atlasWidth = 512;
	int atlasHeight = 512;
	double pixelRange = 4.0;  // Distance field range in pixels
	int glyphSize = 32;        // Size of each glyph in atlas (pixels)

	// Character set to include (ASCII printable)
	const char* charset =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
		"0123456789 !@#$%^&*()_+-=[]{}|;':\",./<>?`~\\";
};

namespace {

	constexpr double kSide = 1e-9; // em either side of an edge, to tell which side the fill is on

	// Under the nonzero rule, which is how TrueType outlines fill.
	bool isFilled(const msdfgen::Shape& shape, msdfgen::Point2 point) {
		int winding = 0;
		for (const msdfgen::Contour& contour : shape.contours) {
			for (const msdfgen::EdgeHolder& edge : contour.edges) {
				double	  x[3];
				int		  dy[3];
				const int crossings = edge->scanlineIntersections(x, dy, point.y);
				for (int i = 0; i < crossings; ++i) {
					winding += x[i] < point.x ? dy[i] : 0;
				}
			}
		}
		return winding != 0;
	}

	// Which side of the edge at t the fill lies on: 1 left, -1 right, 0 both or neither.
	int fillSide(const msdfgen::Shape& shape, const msdfgen::EdgeSegment& edge, double t) {
		const msdfgen::Point2  point = edge.point(t);
		const msdfgen::Vector2 normal = edge.direction(t).getOrthonormal();
		return static_cast<int>(isFilled(shape, point + kSide * normal)) - static_cast<int>(isFilled(shape, point - kSide * normal));
	}

	// Where a horizontal line at y enters and leaves the fill, left to right. Two contours that
	// abut leave a gap of no width between them, which is no edge at all.
	std::vector<double> fillEdges(const msdfgen::Shape& shape, double y) {
		struct Crossing {
			double x;
			int	   dy;
		};
		std::vector<Crossing> crossings;
		for (const msdfgen::Contour& contour : shape.contours) {
			for (const msdfgen::EdgeHolder& edge : contour.edges) {
				double	  x[3];
				int		  dy[3];
				const int count = edge->scanlineIntersections(x, dy, y);
				for (int i = 0; i < count; ++i) {
					crossings.push_back({x[i], dy[i]});
				}
			}
		}
		std::sort(crossings.begin(), crossings.end(), [](const Crossing& a, const Crossing& b) { return a.x < b.x; });
		std::vector<double> edges;
		int					winding = 0;
		for (const Crossing& crossing : crossings) {
			const bool wasFilled = winding != 0;
			winding += crossing.dy;
			if (wasFilled == (winding != 0)) {
				continue;
			}
			if (!edges.empty() && crossing.x - edges.back() < kSide) {
				edges.pop_back();
			} else {
				edges.push_back(crossing.x);
			}
		}
		return edges;
	}

	struct BoundedEdge {
		const msdfgen::EdgeSegment* segment;
		double						left;
		double						bottom;
		double						right;
		double						top;
	};

	// Whether edges[index] at t is outline: fill on one side of it only, and, where edges run along
	// one another (two strokes of Roboto's 'A' share the apex's top), the first of them.
	bool isOnOutline(const msdfgen::Shape& shape, const std::vector<BoundedEdge>& edges, size_t index, double t) {
		const msdfgen::EdgeSegment& edge = *edges[index].segment;
		if (fillSide(shape, edge, t) == 0) {
			return false;
		}
		const msdfgen::Point2 point = edge.point(t);
		for (size_t other = 0; other < index; ++other) {
			const BoundedEdge& e = edges[other];
			if (point.x < e.left - kSide || point.x > e.right + kSide || point.y < e.bottom - kSide || point.y > e.top + kSide) {
				continue;
			}
			// Along it, not at the vertex it shares with this edge.
			double param = 0.0;
			if (std::abs(e.segment->signedDistance(point, param).distance) < kSide && param > 0.0 && param < 1.0) {
				return false;
			}
		}
		return true;
	}

	// The part of the edge from t0 to t1, by de Casteljau.
	msdfgen::EdgeSegment* subEdge(const msdfgen::EdgeSegment& edge, double t0, double t1) {
		const int		degree = edge.type();
		msdfgen::Point2 p[4];
		std::copy_n(edge.controlPoints(), degree + 1, p);
		for (int level = 1; level <= degree; ++level) {
			for (int i = degree; i >= level; --i) {
				p[i] = msdfgen::mix(p[i - 1], p[i], t1);
			}
		}
		const double s = t0 / t1;
		for (int level = 1; level <= degree; ++level) {
			for (int i = 0; i <= degree - level; ++i) {
				p[i] = msdfgen::mix(p[i], p[i + 1], s);
			}
		}
		switch (degree) {
			case 1:
				return msdfgen::EdgeSegment::create(p[0], p[1], edge.color);
			case 2:
				return msdfgen::EdgeSegment::create(p[0], p[1], p[2], edge.color);
			default:
				return msdfgen::EdgeSegment::create(p[0], p[1], p[2], p[3], edge.color);
		}
	}

	// msdfgen takes every edge for outline, so where contours overlap, or one crosses itself (as
	// most of Roboto's do, the 'e' among them), the edges running inside the fill put a false
	// outline in the field: holes in the stroke at the join and a smudge beside it. This cuts each
	// edge where it enters or leaves the fill, drops the parts inside, and chains the rest into
	// closed contours: the outline the fill has. Each part keeps its direction, so the fill stays
	// on the side msdfgen expects. Returns false, the shape untouched, when no edge runs inside
	// the fill for more than a cusp's width, and false with an error, the shape untouched too, when
	// the outline it built does not fill what the contours did or runs the wrong way round.
	bool resolveOverlaps(msdfgen::Shape& shape, std::string& error) {
		constexpr double		 kStep = 0.001;	  // em between samples along an edge
		constexpr double		 kEnd = 1e-6;	  // em short of a vertex, for the samples at an edge's ends
		constexpr double		 kSliver = 1e-6;  // em: a part this short has no direction to speak of
		constexpr double		 kOverlap = 1e-4; // em a dropped part has to run to be an overlap; a cusp drops less
		constexpr double		 kJoin = 1e-4;	  // em: the widest gap the chain bridges, as a tangent contact leaves
		std::vector<BoundedEdge> edges;
		for (const msdfgen::Contour& contour : shape.contours) {
			for (const msdfgen::EdgeHolder& edge : contour.edges) {
				BoundedEdge& bounded = edges.emplace_back();
				bounded.segment = edge;
				const msdfgen::Point2 start = edge->point(0);
				bounded.left = bounded.right = start.x;
				bounded.bottom = bounded.top = start.y;
				edge->bound(bounded.left, bounded.bottom, bounded.right, bounded.top);
			}
		}

		std::vector<msdfgen::EdgeHolder> parts;
		bool							 overlaps = false;
		for (size_t index = 0; index < edges.size(); ++index) {
			const BoundedEdge&			edge = edges[index];
			const msdfgen::EdgeSegment& segment = *edge.segment;
			const double				extent = std::hypot(edge.right - edge.left, edge.top - edge.bottom);
			const int					steps = std::max(8, static_cast<int>(std::ceil(extent / kStep)));
			// Step centres, and each end short of its vertex by kEnd along the edge times its degree
			// (msdfgen's direction is the derivative over the degree), where a contour often starts
			// inside another.
			const double		startT = std::min(kEnd / segment.direction(0).length(), 0.25 / steps);
			const double		endT = std::min(kEnd / segment.direction(1).length(), 0.25 / steps);
			std::vector<double> ts{startT};
			for (int i = 0; i < steps; ++i) {
				ts.push_back((i + 0.5) / steps);
			}
			ts.push_back(1.0 - endT);
			std::vector<char> outline(ts.size());
			for (size_t i = 0; i < ts.size(); ++i) {
				outline[i] = isOnOutline(shape, edges, index, ts[i]);
			}
			std::vector<double> cuts{0.0};
			for (size_t i = 0; i + 1 < ts.size(); ++i) {
				if (outline[i] == outline[i + 1]) {
					continue;
				}
				double lo = ts[i];
				double hi = ts[i + 1];
				while (hi - lo > 1e-12) {
					const double mid = 0.5 * (lo + hi);
					(isOnOutline(shape, edges, index, mid) == static_cast<bool>(outline[i]) ? lo : hi) = mid;
				}
				cuts.push_back(0.5 * (lo + hi));
			}
			cuts.push_back(1.0);
			bool keep = outline[0] != 0;
			for (size_t j = 0; j + 1 < cuts.size(); ++j, keep = !keep) {
				if (!keep) {
					overlaps = overlaps || (segment.point(cuts[j + 1]) - segment.point(cuts[j])).length() >= kOverlap;
				} else if (cuts.size() == 2) {
					parts.emplace_back(segment.clone());
				} else {
					msdfgen::EdgeHolder part(subEdge(segment, cuts[j], cuts[j + 1]));
					// A sliver left by a crossing at a vertex would have no direction; the chain
					// below steps over the gap it leaves.
					if ((part->point(1) - part->point(0)).length() >= kSliver) {
						parts.push_back(std::move(part));
					}
				}
			}
		}
		if (!overlaps) {
			return false;
		}

		msdfgen::Shape outlined;
		outlined.inverseYAxis = shape.inverseYAxis;
		std::vector<char> used(parts.size());
		for (size_t first = 0; first < parts.size(); ++first) {
			if (used[first]) {
				continue;
			}
			msdfgen::Contour& contour = outlined.addContour();
			size_t			  current = first;
			for (;;) {
				used[current] = 1;
				contour.addEdge(std::move(parts[current]));
				// On to the nearest free start, or round to this contour's own if that is nearer.
				const msdfgen::Point2 tip = contour.edges.back()->point(1);
				size_t				  next = parts.size();
				double				  gap = contour.edges.size() > 1 ? (tip - contour.edges.front()->point(0)).length() : kJoin;
				for (size_t k = 0; k < parts.size(); ++k) {
					if (!used[k] && (parts[k]->point(0) - tip).length() < gap) {
						next = k;
						gap = (parts[k]->point(0) - tip).length();
					}
				}
				if (gap >= kJoin) {
					error = "an outline part ends where no other starts";
					return false;
				}
				if (next == parts.size()) {
					contour.edges.front()->moveStartPoint(tip);
					break;
				}
				parts[next]->moveStartPoint(tip);
				current = next;
			}
		}

		// The outline has to fill what the contours did, row by row across the glyph, and keep the
		// fill on the side of each contour that the font's edges have it.
		const msdfgen::Shape::Bounds bounds = shape.getBounds();
		constexpr int				 kRows = 256;
		for (int row = 0; row < kRows; ++row) {
			const double			  y = bounds.b + (bounds.t - bounds.b) * (row + 0.37) / kRows;
			const std::vector<double> before = fillEdges(shape, y);
			const std::vector<double> after = fillEdges(outlined, y);
			bool					  same = before.size() == after.size();
			for (size_t k = 0; same && k < before.size(); ++k) {
				same = std::abs(before[k] - after[k]) <= kJoin;
			}
			if (!same) {
				error = "the outline does not fill what the contours did";
				return false;
			}
		}
		int side = 0;
		for (const BoundedEdge& edge : edges) {
			side += fillSide(shape, *edge.segment, 0.5);
		}
		for (const msdfgen::Contour& contour : outlined.contours) {
			if (fillSide(outlined, *contour.edges.front(), 0.5) * side <= 0) {
				error = "an outline contour runs the wrong way round";
				return false;
			}
		}
		shape = std::move(outlined);
		return true;
	}

} // namespace

int main(int argc, char** argv) {
	AtlasConfig config;

	// Parse command line arguments
	for (int i = 1; i < argc; ++i) {
		std::string arg = argv[i];
		if (arg == "--font" && i + 1 < argc) {
			config.fontPath = argv[++i];
		} else if (arg == "--output" && i + 1 < argc) {
			config.outputPath = argv[++i];
		} else if (arg == "--metadata" && i + 1 < argc) {
			config.metadataPath = argv[++i];
		} else if (arg == "--size" && i + 1 < argc) {
			try {
				int size = std::stoi(argv[++i]);
				if (size < 64 || size > 4096) {
					std::cerr << "Error: Atlas size must be between 64 and 4096 pixels\n";
					return 1;
				}
				config.atlasWidth = config.atlasHeight = size;
			} catch (const std::exception& e) {
				std::cerr << "Error: Invalid atlas size value: " << e.what() << "\n";
				return 1;
			}
		} else if (arg == "--help") {
			std::cout << "Usage: generate_sdf_atlas [options]\n";
			std::cout << "Options:\n";
			std::cout << "  --font <path>      Input font file (default: fonts/Roboto-Regular.ttf)\n";
			std::cout << "  --output <path>    Output PNG file (default: fonts/Roboto-SDF.png)\n";
			std::cout << "  --metadata <path>  Output JSON file (default: fonts/Roboto-SDF.json)\n";
			std::cout << "  --size <pixels>    Atlas size (default: 512)\n";
			std::cout << "  --help             Show this help\n";
			return 0;
		}
	}

	std::cout << "Generating SDF atlas for: " << config.fontPath << "\n";
	std::cout << "Output: " << config.outputPath << "\n";
	std::cout << "Metadata: " << config.metadataPath << "\n";
	std::cout << "Atlas size: " << config.atlasWidth << "x" << config.atlasHeight << "\n\n";

	// Initialize FreeType
	msdfgen::FreetypeHandle* ft = msdfgen::initializeFreetype();
	if (!ft) {
		std::cerr << "ERROR: Could not initialize FreeType\n";
		return 1;
	}

	// Load font
	msdfgen::FontHandle* font = msdfgen::loadFont(ft, config.fontPath);
	if (!font) {
		std::cerr << "ERROR: Could not load font: " << config.fontPath << "\n";
		msdfgen::deinitializeFreetype(ft);
		return 1;
	}

	std::cout << "Font loaded successfully\n";

	// Get font metrics
	msdfgen::FontMetrics metrics;
	if (!msdfgen::getFontMetrics(metrics, font, msdfgen::FONT_SCALING_EM_NORMALIZED)) {
		std::cerr << "ERROR: Could not get font metrics\n";
		msdfgen::destroyFont(font);
		msdfgen::deinitializeFreetype(ft);
		return 1;
	}

	std::cout << "Font metrics: emSize=" << metrics.emSize
	          << " ascender=" << metrics.ascenderY
	          << " descender=" << metrics.descenderY << "\n";

	// Load glyphs
	std::vector<GlyphData> glyphs;
	int glyphCount = 0;

	// The characters whose overlapping contours were resolved, for the summary
	std::string resolvedGlyphs;

	for (const char* p = config.charset; *p; ++p) {
		char c = *p;
		msdfgen::unicode_t unicode = static_cast<msdfgen::unicode_t>(c);

		GlyphData glyph;
		glyph.character = c;
		glyph.unicode = unicode;

		// Load glyph shape
		if (msdfgen::loadGlyph(glyph.shape, font, unicode,
		                       msdfgen::FONT_SCALING_EM_NORMALIZED, &glyph.advance)) {

			// Get glyph bounds
			msdfgen::Shape::Bounds bounds = glyph.shape.getBounds();
			glyph.planeLeft = bounds.l;
			glyph.planeBottom = bounds.b;
			glyph.planeRight = bounds.r;
			glyph.planeTop = bounds.t;

			// Skip whitespace glyphs (they have no shape)
			if (glyph.shape.contours.empty()) {
				// Still add whitespace characters for metadata (like space)
				if (c == ' ' || c == '\t') {
					glyphs.push_back(glyph);
				}
				continue;
			}

			// A glyph that does not resolve keeps the font's contours, and its specks with them.
			std::string error;
			if (resolveOverlaps(glyph.shape, error)) {
				resolvedGlyphs += c;
			} else if (!error.empty()) {
				std::cerr << "WARNING: " << config.fontPath << ": could not resolve the overlapping contours of '" << c << "' (" << error
						  << "), so it keeps the font's\n";
			}

			// Apply edge coloring for MSDF
			msdfgen::edgeColoringSimple(glyph.shape, 3.0);

			glyphs.push_back(glyph);
			glyphCount++;
		}
	}

	std::cout << "Loaded " << glyphCount << " glyphs\n";
	if (!resolvedGlyphs.empty()) {
		std::cout << "Overlapping contours resolved in " << resolvedGlyphs.size() << ": " << resolvedGlyphs << "\n";
	}

	if (glyphs.empty()) {
		std::cerr << "ERROR: No glyphs loaded\n";
		msdfgen::destroyFont(font);
		msdfgen::deinitializeFreetype(ft);
		return 1;
	}

	// Atlas texels per em, the same for every glyph (see the distance field loop below)
	const double uniformScale = static_cast<double>(config.glyphSize) / metrics.emSize;

	// Glyphs sit centred in glyphSize cells, in charset order. The renderer reads a glyph's field
	// out to half the distance range past its bounds, plus half a texel of bilinear footprint
	// (salal's FontRenderer::generateGlyphQuads pads each quad by the half range), and that has to
	// lie in the glyph's own cell: one without the room (parentheses, brackets, '|', '$', 'j')
	// picked up the cells either side of it, or was cut off at its own edge. Such a glyph gets a
	// cell grown by whole texels on each side, which keeps it where it was on the texel grid, on
	// shelves after the grid's rows (above them in the PNG, the bitmap being y-up); its grid cell
	// stays empty, so no other glyph moves.
	const double reach = config.pixelRange / 2.0 + 0.5;

	const auto cellFor = [&](const GlyphData& glyph) {
		const double extent = std::max(glyph.planeRight - glyph.planeLeft, glyph.planeTop - glyph.planeBottom) * uniformScale;
		const double shortfall = std::max(0.0, extent + 2.0 * reach - config.glyphSize);
		return config.glyphSize + 2 * static_cast<int>(std::ceil(shortfall / 2.0));
	};

	int currentX = 0;
	int currentY = 0;
	std::vector<GlyphData*> tallGlyphs;
	for (auto& glyph : glyphs) {
		if (glyph.shape.contours.empty()) {
			// Whitespace - no atlas space needed
			continue;
		}
		if (currentX + config.glyphSize > config.atlasWidth) {
			currentX = 0;
			currentY += config.glyphSize;
		}
		glyph.atlasX = currentX;
		glyph.atlasY = currentY;
		glyph.atlasWidth = config.glyphSize;
		glyph.atlasHeight = config.glyphSize;
		currentX += config.glyphSize;
		if (cellFor(glyph) > config.glyphSize) {
			tallGlyphs.push_back(&glyph);
		}
	}

	currentX = 0;
	currentY += config.glyphSize;
	int shelfHeight = 0;
	for (GlyphData* glyph : tallGlyphs) {
		const int cell = cellFor(*glyph);
		if (currentX + cell > config.atlasWidth) {
			currentX = 0;
			currentY += shelfHeight;
			shelfHeight = 0;
		}
		glyph->atlasX = currentX;
		glyph->atlasY = currentY;
		glyph->atlasWidth = cell;
		glyph->atlasHeight = cell;
		currentX += cell;
		shelfHeight = std::max(shelfHeight, cell);
	}
	if (currentY + shelfHeight > config.atlasHeight) {
		std::cerr << "ERROR: Atlas too small for all glyphs\n";
		msdfgen::destroyFont(font);
		msdfgen::deinitializeFreetype(ft);
		return 1;
	}

	std::cout << "Glyphs packed into atlas (" << tallGlyphs.size() << " in larger cells)\n";

	// Create atlas bitmap
	msdfgen::Bitmap<float, 3> atlas(config.atlasWidth, config.atlasHeight);

	// Initialize to background color (0,0,0)
	for (int y = 0; y < config.atlasHeight; ++y) {
		for (int x = 0; x < config.atlasWidth; ++x) {
			float* pixel = atlas(x, y);
			pixel[0] = 0.0f;
			pixel[1] = 0.0f;
			pixel[2] = 0.0f;
		}
	}

	std::cout << "Generating distance fields...\n";

	// Generate MSDF for each glyph and blit into atlas
	//
	// CRITICAL: Use UNIFORM scaling for all glyphs, not per-glyph scaling
	// Reference: https://github.com/Chlumsky/msdf-atlas-gen
	// The official msdf-atlas-gen uses wrapBox() with a uniform glyphScale
	// This ensures plane bounds (in EM units) correctly map to atlas pixels
	//
	// Uniform scale = glyphSize (pixels) / emSize (EM units)
	// For glyphSize=32 and emSize=1.0: scale = 32.0
	// This means: 1 EM = 32 pixels in atlas
	// Example: A glyph 0.6 EM wide occupies 0.6 * 32 = 19.2 pixels in atlas

	int processedCount = 0;
	for (auto& glyph : glyphs) {
		if (glyph.shape.contours.empty()) {
			continue;
		}

		// Create a temporary bitmap for this glyph
		msdfgen::Bitmap<float, 3> glyphBitmap(glyph.atlasWidth, glyph.atlasHeight);

		// Calculate glyph dimensions in EM units
		double glyphWidth = glyph.planeRight - glyph.planeLeft;
		double glyphHeight = glyph.planeTop - glyph.planeBottom;

		// Center the glyph in its atlas cell using uniform scale
		// translateX/Y position the glyph so its center aligns with cell center
		double translateX = -glyph.planeLeft + (glyph.atlasWidth / uniformScale - glyphWidth) / 2.0;
		double translateY = -glyph.planeBottom + (glyph.atlasHeight / uniformScale - glyphHeight) / 2.0;

		// Set up transformation using UNIFORM scale for all glyphs
		// Reference: https://github.com/Chlumsky/msdf-atlas-gen/discussions/47
		// The scale vector transforms EM units to atlas pixels
		// The range determines SDF sample area: pixelRange / scale
		msdfgen::Vector2 scaleVec(uniformScale, uniformScale);
		msdfgen::Vector2 translateVec(translateX, translateY);
		msdfgen::Projection projection(scaleVec, translateVec);
		msdfgen::Range range(config.pixelRange / uniformScale);
		msdfgen::SDFTransformation transformation(projection, msdfgen::DistanceMapping(range));

		// Calculate actual glyph bounds within the atlas cell (in pixels)
		// Apply transformation to plane bounds to get pixel coordinates
		// Reference: https://github.com/Chlumsky/msdf-atlas-gen/issues/2
		// atlasBounds defines where the actual rendered glyph is within the cell
		glyph.atlasBoundsLeft = (glyph.planeLeft + translateX) * uniformScale;
		glyph.atlasBoundsBottom = (glyph.planeBottom + translateY) * uniformScale;
		glyph.atlasBoundsRight = (glyph.planeRight + translateX) * uniformScale;
		glyph.atlasBoundsTop = (glyph.planeTop + translateY) * uniformScale;

		// Generate MSDF
		msdfgen::MSDFGeneratorConfig genConfig;
		msdfgen::generateMSDF(glyphBitmap, glyph.shape, transformation, genConfig);

		// Blit into atlas
		for (int y = 0; y < glyph.atlasHeight; ++y) {
			for (int x = 0; x < glyph.atlasWidth; ++x) {
				const float* src = glyphBitmap(x, y);
				float* dst = atlas(glyph.atlasX + x, glyph.atlasY + y);
				dst[0] = src[0];
				dst[1] = src[1];
				dst[2] = src[2];
			}
		}

		processedCount++;
		if (processedCount % 10 == 0) {
			std::cout << "  Processed " << processedCount << "/" << glyphCount << " glyphs\n";
		}
	}

	std::cout << "Distance fields generated (" << processedCount << " glyphs)\n";

	// Save atlas as PNG
	if (!msdfgen::savePng(atlas, config.outputPath)) {
		std::cerr << "ERROR: Failed to save PNG: " << config.outputPath << "\n";
		msdfgen::destroyFont(font);
		msdfgen::deinitializeFreetype(ft);
		return 1;
	}

	std::cout << "PNG atlas saved: " << config.outputPath << "\n";

	// Export JSON metadata
	std::ofstream jsonFile(config.metadataPath);
	if (!jsonFile.is_open()) {
		std::cerr << "ERROR: Could not open metadata file: " << config.metadataPath << "\n";
		msdfgen::destroyFont(font);
		msdfgen::deinitializeFreetype(ft);
		return 1;
	}

	jsonFile << "{\n";
	jsonFile << "  \"atlas\": {\n";
	jsonFile << "    \"type\": \"msdf\",\n";
	jsonFile << "    \"distanceRange\": " << config.pixelRange << ",\n";
	jsonFile << "    \"size\": " << config.glyphSize << ",\n";
	jsonFile << "    \"width\": " << config.atlasWidth << ",\n";
	jsonFile << "    \"height\": " << config.atlasHeight << "\n";
	jsonFile << "  },\n";
	jsonFile << "  \"metrics\": {\n";
	jsonFile << "    \"emSize\": " << metrics.emSize << ",\n";
	jsonFile << "    \"ascender\": " << metrics.ascenderY << ",\n";
	jsonFile << "    \"descender\": " << metrics.descenderY << ",\n";
	jsonFile << "    \"lineHeight\": " << metrics.lineHeight << "\n";
	jsonFile << "  },\n";
	jsonFile << "  \"glyphs\": {\n";

	bool first = true;
	for (const auto& glyph : glyphs) {
		if (!first) jsonFile << ",\n";
		first = false;

		// Escape special JSON characters
		std::string charStr;
		char c = glyph.character;
		if (c == '\"') charStr = "\\\"";
		else if (c == '\\') charStr = "\\\\";
		else if (c == '\n') charStr = "\\n";
		else if (c == '\r') charStr = "\\r";
		else if (c == '\t') charStr = "\\t";
		else charStr = std::string(1, c);

		jsonFile << "    \"" << charStr << "\": {\n";

		// Atlas cell coordinates (normalized 0-1) - the full allocated cell
		if (!glyph.shape.contours.empty()) {
			double normX = static_cast<double>(glyph.atlasX) / config.atlasWidth;
			double normY = static_cast<double>(glyph.atlasY) / config.atlasHeight;
			double normW = static_cast<double>(glyph.atlasWidth) / config.atlasWidth;
			double normH = static_cast<double>(glyph.atlasHeight) / config.atlasHeight;

			jsonFile << "      \"atlas\": {";
			jsonFile << "\"x\": " << normX << ", ";
			jsonFile << "\"y\": " << normY << ", ";
			jsonFile << "\"width\": " << normW << ", ";
			jsonFile << "\"height\": " << normH << "},\n";

			// Atlas bounds (normalized 0-1) - actual glyph content within the cell
			// Reference: https://github.com/Chlumsky/msdf-atlas-gen/issues/2
			// These are the pixel coordinates where the actual glyph is rendered
			double atlasBoundsLeft = (glyph.atlasX + glyph.atlasBoundsLeft) / config.atlasWidth;
			double atlasBoundsBottom = (glyph.atlasY + glyph.atlasBoundsBottom) / config.atlasHeight;
			double atlasBoundsRight = (glyph.atlasX + glyph.atlasBoundsRight) / config.atlasWidth;
			double atlasBoundsTop = (glyph.atlasY + glyph.atlasBoundsTop) / config.atlasHeight;

			jsonFile << "      \"atlasBounds\": {";
			jsonFile << "\"left\": " << atlasBoundsLeft << ", ";
			jsonFile << "\"bottom\": " << atlasBoundsBottom << ", ";
			jsonFile << "\"right\": " << atlasBoundsRight << ", ";
			jsonFile << "\"top\": " << atlasBoundsTop << "},\n";

			jsonFile << "      \"plane\": {";
			jsonFile << "\"left\": " << glyph.planeLeft << ", ";
			jsonFile << "\"bottom\": " << glyph.planeBottom << ", ";
			jsonFile << "\"right\": " << glyph.planeRight << ", ";
			jsonFile << "\"top\": " << glyph.planeTop << "},\n";
		} else {
			// Whitespace - no atlas coordinates
			jsonFile << "      \"atlas\": null,\n";
			jsonFile << "      \"atlasBounds\": null,\n";
			jsonFile << "      \"plane\": null,\n";
		}

		jsonFile << "      \"advance\": " << glyph.advance << "\n";
		jsonFile << "    }";
	}

	jsonFile << "\n  }\n";
	jsonFile << "}\n";
	jsonFile.close();

	std::cout << "JSON metadata saved: " << config.metadataPath << "\n";

	// Cleanup
	msdfgen::destroyFont(font);
	msdfgen::deinitializeFreetype(ft);

	std::cout << "\nSDF atlas generation complete!\n";
	return 0;
}
