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

	// Atlas coordinates (in pixels)
	int atlasX = 0;
	int atlasY = 0;
	int atlasWidth = 0;
	int atlasHeight = 0;

	// Plane bounds (in font units)
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
			config.atlasWidth = config.atlasHeight = std::stoi(argv[++i]);
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

			// Apply edge coloring for MSDF
			msdfgen::edgeColoringSimple(glyph.shape, 3.0);

			glyphs.push_back(glyph);
			glyphCount++;
		}
	}

	std::cout << "Loaded " << glyphCount << " glyphs\n";

	if (glyphs.empty()) {
		std::cerr << "ERROR: No glyphs loaded\n";
		msdfgen::destroyFont(font);
		msdfgen::deinitializeFreetype(ft);
		return 1;
	}

	// Pack glyphs into atlas using simple grid layout
	int glyphsPerRow = config.atlasWidth / config.glyphSize;
	int currentX = 0;
	int currentY = 0;

	for (auto& glyph : glyphs) {
		if (glyph.shape.contours.empty()) {
			// Whitespace - no atlas space needed
			continue;
		}

		glyph.atlasX = currentX;
		glyph.atlasY = currentY;
		glyph.atlasWidth = config.glyphSize;
		glyph.atlasHeight = config.glyphSize;

		currentX += config.glyphSize;
		if (currentX + config.glyphSize > config.atlasWidth) {
			currentX = 0;
			currentY += config.glyphSize;

			if (currentY + config.glyphSize > config.atlasHeight) {
				std::cerr << "WARNING: Atlas too small for all glyphs\n";
				break;
			}
		}
	}

	std::cout << "Glyphs packed into atlas\n";

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
	int processedCount = 0;
	for (const auto& glyph : glyphs) {
		if (glyph.shape.contours.empty()) {
			continue;
		}

		// Create a temporary bitmap for this glyph
		msdfgen::Bitmap<float, 3> glyphBitmap(glyph.atlasWidth, glyph.atlasHeight);

		// Calculate transformation from glyph space to pixel space
		double glyphWidth = glyph.planeRight - glyph.planeLeft;
		double glyphHeight = glyph.planeTop - glyph.planeBottom;

		// Scale to fit in glyph size with some padding
		double padding = 0.1;  // 10% padding
		double scale = std::min(
			(glyph.atlasWidth * (1.0 - 2.0 * padding)) / glyphWidth,
			(glyph.atlasHeight * (1.0 - 2.0 * padding)) / glyphHeight
		);

		// Center the glyph
		double translateX = -glyph.planeLeft + (glyph.atlasWidth / scale - glyphWidth) / 2.0;
		double translateY = -glyph.planeBottom + (glyph.atlasHeight / scale - glyphHeight) / 2.0;

		// Set up transformation
		msdfgen::Vector2 scaleVec(scale, scale);
		msdfgen::Vector2 translateVec(translateX, translateY);
		msdfgen::Projection projection(scaleVec, translateVec);
		msdfgen::Range range(config.pixelRange / scale);
		msdfgen::SDFTransformation transformation(projection, msdfgen::DistanceMapping(range));

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

		// Atlas bounds (normalized 0-1)
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

			jsonFile << "      \"plane\": {";
			jsonFile << "\"left\": " << glyph.planeLeft << ", ";
			jsonFile << "\"bottom\": " << glyph.planeBottom << ", ";
			jsonFile << "\"right\": " << glyph.planeRight << ", ";
			jsonFile << "\"top\": " << glyph.planeTop << "},\n";
		} else {
			// Whitespace - no atlas coordinates
			jsonFile << "      \"atlas\": null,\n";
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
