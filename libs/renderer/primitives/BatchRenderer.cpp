// Uber Batch Renderer implementation.
// Accumulates 2D geometry (shapes + text) and renders in optimized batches.

#include "primitives/BatchRenderer.h"
#include "CoordinateSystem/CoordinateSystem.h"
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <algorithm>

namespace Renderer {

	namespace {
		// Render mode constant for tile rendering. Must match uber.vert:31 and uber.frag:66.
		constexpr float kRenderModeTile = -3.0F;
		// Maximum number of tile atlas UV rects. Must match uber.frag:19 (u_tileAtlasRects[64]).
		constexpr int kMaxTileAtlasRects = 64;

		// Helper to transform a 2D position by a 4x4 matrix
		// isIdentity flag is pre-computed in setTransform() to avoid per-vertex checks
		inline Foundation::Vec2 TransformPosition(const Foundation::Vec2& pos, const Foundation::Mat4& transform, bool isIdentity) {
			if (isIdentity) {
				return pos;
			}
			// Apply transform: multiply position by matrix
			Foundation::Vec4 result = transform * Foundation::Vec4(pos.x, pos.y, 0.0F, 1.0F);
			return Foundation::Vec2(result.x, result.y);
		}
	} // namespace

	BatchRenderer::BatchRenderer() { // NOLINT(cppcoreguidelines-pro-type-member-init,modernize-use-equals-default)
		// Reserve space for vertices to minimize allocations
		vertices.reserve(10000);
		indices.reserve(15000);
	}

	BatchRenderer::~BatchRenderer() {
		shutdown();
	}

	void BatchRenderer::init() {
		// Load uber shader (unified shapes + text)
		if (!shader.LoadFromFile("uber.vert", "uber.frag")) {
			std::cerr << "Failed to load uber shaders!" << std::endl;
			return;
		}

		// Get uniform locations (standard batched rendering)
		projectionLoc = glGetUniformLocation(shader.getProgram(), "u_projection");
		transformLoc = glGetUniformLocation(shader.getProgram(), "u_transform");
		atlasLoc = glGetUniformLocation(shader.getProgram(), "u_atlas");
		viewportHeightLoc = glGetUniformLocation(shader.getProgram(), "u_viewportHeight");
		pixelRatioLoc = glGetUniformLocation(shader.getProgram(), "u_pixelRatio");
		tileAtlasLoc = glGetUniformLocation(shader.getProgram(), "u_tileAtlas");
		tileAtlasRectsLoc = glGetUniformLocation(shader.getProgram(), "u_tileAtlasRects");
		tileAtlasCountLoc = glGetUniformLocation(shader.getProgram(), "u_tileAtlasRectCount");

		// Get uniform locations (instanced rendering)
		cameraPositionLoc = glGetUniformLocation(shader.getProgram(), "u_cameraPosition");
		cameraZoomLoc = glGetUniformLocation(shader.getProgram(), "u_cameraZoom");
		pixelsPerMeterLoc = glGetUniformLocation(shader.getProgram(), "u_pixelsPerMeter");
		viewportSizeLoc = glGetUniformLocation(shader.getProgram(), "u_viewportSize");
		instancedLoc = glGetUniformLocation(shader.getProgram(), "u_instanced");

		// Debug: verify instancing uniforms are found (only in debug builds)
#ifndef NDEBUG
		std::cerr << "[BatchRenderer] Instancing uniforms: "
				  << "u_instanced=" << instancedLoc
				  << " u_cameraPosition=" << cameraPositionLoc
				  << " u_cameraZoom=" << cameraZoomLoc
				  << " u_pixelsPerMeter=" << pixelsPerMeterLoc
				  << " u_viewportSize=" << viewportSizeLoc << std::endl;
#endif

		// Create VAO/VBO/IBO
		glGenVertexArrays(1, &vao);
		glGenBuffers(1, &vbo);
		glGenBuffers(1, &ibo);

		glBindVertexArray(vao);

		// Set up vertex buffer
		glBindBuffer(GL_ARRAY_BUFFER, vbo);

		// Position attribute (location = 0)
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(UberVertex), (void*)offsetof(UberVertex, position));

		// TexCoord attribute (location = 1) - UV for text, rectLocalPos for shapes
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(UberVertex), (void*)offsetof(UberVertex, texCoord));

		// Color attribute (location = 2)
		glEnableVertexAttribArray(2);
		glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(UberVertex), (void*)offsetof(UberVertex, color));

		// Data1 attribute (location = 3) - borderData for shapes
		glEnableVertexAttribArray(3);
		glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(UberVertex), (void*)offsetof(UberVertex, data1));

		// Data2 attribute (location = 4) - shapeParams for shapes, (pixelRange, 0, 0, -1) for text
		glEnableVertexAttribArray(4);
		glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(UberVertex), (void*)offsetof(UberVertex, data2));

		// ClipBounds attribute (location = 5) - (minX, minY, maxX, maxY) for clipping
		glEnableVertexAttribArray(5);
		glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, sizeof(UberVertex), (void*)offsetof(UberVertex, clipBounds));

		// Data3 attribute (location = 8) - diagonal neighbors for tiles (NW, NE, SE, SW)
		// Note: locations 6-7 are reserved for instancing
		glEnableVertexAttribArray(8);
		glVertexAttribPointer(8, 4, GL_FLOAT, GL_FALSE, sizeof(UberVertex), (void*)offsetof(UberVertex, data3));

		// Bind index buffer
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);

		glBindVertexArray(0);
	}

	void BatchRenderer::shutdown() {
		if (vao != 0) {
			glDeleteVertexArrays(1, &vao);
			vao = 0;
		}

		if (vbo != 0) {
			glDeleteBuffers(1, &vbo);
			vbo = 0;
		}

		if (ibo != 0) {
			glDeleteBuffers(1, &ibo);
			ibo = 0;
		}

		// Shader cleanup handled by RAII destructor
	}

	void BatchRenderer::addQuad( // NOLINT(readability-convert-member-functions-to-static)
		const Foundation::Rect&						bounds,
		const Foundation::Color&					fillColor,
		const std::optional<Foundation::BorderStyle>& border,
		float										cornerRadius
	) { // NOLINT(readability-convert-member-functions-to-static)
		uint32_t baseIndex = static_cast<uint32_t>(vertices.size());

		// Calculate rect center and half-dimensions for SDF
		float halfW = bounds.width * 0.5F;
		float halfH = bounds.height * 0.5F;

		// Fill color
		Foundation::Vec4 colorVec = fillColor.toVec4();

		// Pack border data (color RGB + width)
		Foundation::Vec4 borderData(0.0F, 0.0F, 0.0F, 0.0F);
		float			 borderWidth = 0.0F;
		float			 borderPosEnum = 1.0F; // Default to Center
		if (border.has_value()) {
			borderData = Foundation::Vec4(border->color.r, border->color.g, border->color.b, border->width);
			borderWidth = border->width;
			// Use corner radius from border if provided
			if (border->cornerRadius > 0.0F) {
				cornerRadius = border->cornerRadius;
			}
			switch (border->position) {
				case Foundation::BorderPosition::Inside:
					borderPosEnum = 0.0F;
					break;
				case Foundation::BorderPosition::Center:
					borderPosEnum = 1.0F;
					break;
				case Foundation::BorderPosition::Outside:
					borderPosEnum = 2.0F;
					break;
			}
		}

		// Calculate how much the border extends outside the shape bounds
		// Inside (0): border entirely inside, no expansion needed
		// Center (1): border straddles edge, half extends outside
		// Outside (2): border entirely outside, full width extends outside
		float borderOuterExtent = 0.0F;
		if (borderPosEnum == 1.0F) {
			borderOuterExtent = borderWidth * 0.5F; // Center: half outside
		} else if (borderPosEnum == 2.0F) {
			borderOuterExtent = borderWidth; // Outside: full width outside
		}

		// Expand the quad to cover the border that extends outside the shape
		float expandedHalfW = halfW + borderOuterExtent;
		float expandedHalfH = halfH + borderOuterExtent;

		// Calculate expanded screen-space bounds
		float centerX = bounds.x + halfW;
		float centerY = bounds.y + halfH;

		// Pack shape parameters (halfWidth, halfHeight, cornerRadius, borderPosition)
		// Note: shapeParams still uses the ORIGINAL halfW/halfH for SDF calculation
		Foundation::Vec4 shapeParams(halfW, halfH, cornerRadius, borderPosEnum);

		// Add 4 vertices with expanded screen positions but rect-local coordinates
		// that extend beyond the original shape bounds.
		// Positions are transformed by currentTransform to support scrolling/offset.
		Foundation::Vec4 noData3(0.0F, 0.0F, 0.0F, 0.0F); // Unused for shapes

		// Top-left corner
		vertices.push_back(
			{TransformPosition(Foundation::Vec2(centerX - expandedHalfW, centerY - expandedHalfH), currentTransform, transformIsIdentity),
			 Foundation::Vec2(-expandedHalfW, -expandedHalfH), // Rect-local: top-left (expanded)
			 colorVec,
			 borderData,
			 shapeParams,
			 currentClipBounds,
			 noData3}
		);

		// Top-right corner
		vertices.push_back(
			{TransformPosition(Foundation::Vec2(centerX + expandedHalfW, centerY - expandedHalfH), currentTransform, transformIsIdentity),
			 Foundation::Vec2(expandedHalfW, -expandedHalfH), // Rect-local: top-right (expanded)
			 colorVec,
			 borderData,
			 shapeParams,
			 currentClipBounds,
			 noData3}
		);

		// Bottom-right corner
		vertices.push_back(
			{TransformPosition(Foundation::Vec2(centerX + expandedHalfW, centerY + expandedHalfH), currentTransform, transformIsIdentity),
			 Foundation::Vec2(expandedHalfW, expandedHalfH), // Rect-local: bottom-right (expanded)
			 colorVec,
			 borderData,
			 shapeParams,
			 currentClipBounds,
			 noData3}
		);

		// Bottom-left corner
		vertices.push_back(
			{TransformPosition(Foundation::Vec2(centerX - expandedHalfW, centerY + expandedHalfH), currentTransform, transformIsIdentity),
			 Foundation::Vec2(-expandedHalfW, expandedHalfH), // Rect-local: bottom-left (expanded)
			 colorVec,
			 borderData,
			 shapeParams,
			 currentClipBounds,
			 noData3}
		);

		// Add 6 indices (2 triangles)
		indices.push_back(baseIndex + 0);
		indices.push_back(baseIndex + 1);
		indices.push_back(baseIndex + 2);

		indices.push_back(baseIndex + 0);
		indices.push_back(baseIndex + 2);
		indices.push_back(baseIndex + 3);
	}


	void BatchRenderer::addTileQuad(
		const Foundation::Rect&  bounds,
		const Foundation::Color& color,
		uint8_t				 edgeMask,
		uint8_t				 cornerMask,
		uint8_t			 surfaceId,
		uint8_t			 hardEdgeMask,
		int32_t			 tileX,
		int32_t			 tileY,
		uint8_t			 neighborN,
		uint8_t			 neighborE,
		uint8_t			 neighborS,
		uint8_t			 neighborW,
		uint8_t			 neighborNW,
		uint8_t			 neighborNE,
		uint8_t			 neighborSE,
		uint8_t			 neighborSW
	) { // NOLINT(readability-convert-member-functions-to-static)
		uint32_t baseIndex = static_cast<uint32_t>(vertices.size());

		float halfW = bounds.width * 0.5F;
		float halfH = bounds.height * 0.5F;
		float centerX = bounds.x + halfW;
		float centerY = bounds.y + halfH;

		// Pack tile coordinates into a single float for shader use.
		// Offset by 32768 to handle negative coordinates, then pack X in lower 16 bits, Y in upper 16 bits.
		// This supports world coordinates from -32768 to +32767 tiles in each dimension.
		auto packedTileCoord = static_cast<float>(
			(static_cast<uint32_t>(tileX + 32768) & 0xFFFFU) |
			((static_cast<uint32_t>(tileY + 32768) & 0xFFFFU) << 16U)
		);

		Foundation::Vec4 colorVec = color.toVec4();
		Foundation::Vec4 data1(static_cast<float>(edgeMask), static_cast<float>(cornerMask), static_cast<float>(surfaceId), static_cast<float>(hardEdgeMask));
		Foundation::Vec4 data2(halfW, halfH, packedTileCoord, kRenderModeTile);

		// For tiles, repurpose clipBounds to store cardinal neighbor surface IDs for soft edge blending.
		// Tiles don't use per-vertex clipping (the shader returns before the clip check for tiles).
		// Each neighbor ID is a surface type (0-255), stored as float for shader compatibility.
		Foundation::Vec4 neighborData(
			static_cast<float>(neighborN),
			static_cast<float>(neighborE),
			static_cast<float>(neighborS),
			static_cast<float>(neighborW)
		);

		// Diagonal neighbor surface IDs for corner blending
		Foundation::Vec4 diagonalData(
			static_cast<float>(neighborNW),
			static_cast<float>(neighborNE),
			static_cast<float>(neighborSE),
			static_cast<float>(neighborSW)
		);

		// Top-left
		vertices.push_back({
			TransformPosition(Foundation::Vec2(centerX - halfW, centerY - halfH), currentTransform, transformIsIdentity),
			Foundation::Vec2(-halfW, -halfH),
			colorVec,
			data1,
			data2,
			neighborData,
			diagonalData
		});

		// Top-right
		vertices.push_back({
			TransformPosition(Foundation::Vec2(centerX + halfW, centerY - halfH), currentTransform, transformIsIdentity),
			Foundation::Vec2(halfW, -halfH),
			colorVec,
			data1,
			data2,
			neighborData,
			diagonalData
		});

		// Bottom-right
		vertices.push_back({
			TransformPosition(Foundation::Vec2(centerX + halfW, centerY + halfH), currentTransform, transformIsIdentity),
			Foundation::Vec2(halfW, halfH),
			colorVec,
			data1,
			data2,
			neighborData,
			diagonalData
		});

		// Bottom-left
		vertices.push_back({
			TransformPosition(Foundation::Vec2(centerX - halfW, centerY + halfH), currentTransform, transformIsIdentity),
			Foundation::Vec2(-halfW, halfH),
			colorVec,
			data1,
			data2,
			neighborData,
			diagonalData
		});

		indices.push_back(baseIndex + 0);
		indices.push_back(baseIndex + 1);
		indices.push_back(baseIndex + 2);

		indices.push_back(baseIndex + 0);
		indices.push_back(baseIndex + 2);
		indices.push_back(baseIndex + 3);
	}

	void BatchRenderer::addTriangles( // NOLINT(readability-convert-member-functions-to-static)
		const Foundation::Vec2*	  inputVertices,
		const uint16_t*			  inputIndices,
		size_t					  vertexCount,
		size_t					  indexCount,
		const Foundation::Color&  color,
		const Foundation::Color*  inputColors
	) { // NOLINT(readability-convert-member-functions-to-static)
		uint32_t baseIndex = static_cast<uint32_t>(vertices.size());

		Foundation::Vec4 uniformColorVec = color.toVec4();

		// Default data (not used for tessellated shapes, but required for vertex format)
		// Use borderPosition=1 (Center) so shader treats these as shapes, not text
		Foundation::Vec2 zeroVec2(0.0F, 0.0F);
		Foundation::Vec4 zeroVec4(0.0F, 0.0F, 0.0F, 0.0F);
		Foundation::Vec4 shapeParams(0.0F, 0.0F, 0.0F, 1.0F); // borderPos=1 marks as shape

		// Add all vertices (positions transformed by currentTransform)
		for (size_t i = 0; i < vertexCount; ++i) {
			// Use per-vertex color if provided, otherwise uniform color
			Foundation::Vec4 colorVec =
				(inputColors != nullptr) ? inputColors[i].toVec4() : uniformColorVec;

			vertices.push_back({
				TransformPosition(inputVertices[i], currentTransform, transformIsIdentity),
				zeroVec2, // texCoord (unused for triangles)
				colorVec,
				zeroVec4,		  // data1 (unused)
				shapeParams,	  // data2 with borderPos >= 0 marks as shape
				currentClipBounds, // clip bounds
				zeroVec4		  // data3 (unused for triangles)
			});
		}

		// Add all indices (offset by baseIndex)
		for (size_t i = 0; i < indexCount; ++i) {
			indices.push_back(baseIndex + inputIndices[i]);
		}
	}

	void BatchRenderer::addTextQuad(
		const Foundation::Vec2&	 position,
		const Foundation::Vec2&	 size,
		const Foundation::Vec2&	 uvMin,
		const Foundation::Vec2&	 uvMax,
		const Foundation::Color& color
	) {
		uint32_t baseIndex = static_cast<uint32_t>(vertices.size());

		Foundation::Vec4 colorVec = color.toVec4();

		// Text data packing:
		// data1 = unused (0,0,0,0)
		// data2 = (pixelRange, 0, 0, -1) where -1 signals text rendering mode
		Foundation::Vec4 zeroVec4(0.0F, 0.0F, 0.0F, 0.0F);
		Foundation::Vec4 textParams(fontPixelRange, 0.0F, 0.0F, kRenderModeText);

		// Add 4 vertices for glyph quad
		// Note: UV Y coordinates are flipped for OpenGL coordinate system
		// Positions are transformed by currentTransform to support scrolling/offset

		// Top-left
		vertices.push_back(
			{TransformPosition(position, currentTransform, transformIsIdentity),
			 Foundation::Vec2(uvMin.x, uvMax.y), // UV flipped
			 colorVec,
			 zeroVec4,
			 textParams,
			 currentClipBounds,
			 zeroVec4} // data3 (unused for text)
		);

		// Top-right
		vertices.push_back(
			{TransformPosition(Foundation::Vec2(position.x + size.x, position.y), currentTransform, transformIsIdentity),
			 Foundation::Vec2(uvMax.x, uvMax.y), // UV flipped
			 colorVec,
			 zeroVec4,
			 textParams,
			 currentClipBounds,
			 zeroVec4} // data3 (unused for text)
		);

		// Bottom-right
		vertices.push_back(
			{TransformPosition(Foundation::Vec2(position.x + size.x, position.y + size.y), currentTransform, transformIsIdentity),
			 Foundation::Vec2(uvMax.x, uvMin.y), // UV flipped
			 colorVec,
			 zeroVec4,
			 textParams,
			 currentClipBounds,
			 zeroVec4} // data3 (unused for text)
		);

		// Bottom-left
		vertices.push_back(
			{TransformPosition(Foundation::Vec2(position.x, position.y + size.y), currentTransform, transformIsIdentity),
			 Foundation::Vec2(uvMin.x, uvMin.y), // UV flipped
			 colorVec,
			 zeroVec4,
			 textParams,
			 currentClipBounds,
			 zeroVec4} // data3 (unused for text)
		);

		// Add 6 indices (2 triangles)
		indices.push_back(baseIndex + 0);
		indices.push_back(baseIndex + 1);
		indices.push_back(baseIndex + 2);

		indices.push_back(baseIndex + 0);
		indices.push_back(baseIndex + 2);
		indices.push_back(baseIndex + 3);
	}

	void BatchRenderer::setFontAtlas(GLuint atlasTexture, float pixelRange) {
		fontAtlas = atlasTexture;
		fontPixelRange = pixelRange;
	}

	void BatchRenderer::setTileAtlas(GLuint atlasTexture, const std::vector<glm::vec4>& rects) {
		tileAtlas = atlasTexture;
		tileAtlasRects = rects;
	}

	void BatchRenderer::flush() {
		if (vertices.empty()) {
			return;
		}

		// Enable blending for transparency (shapes and text both need this)
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		// Disable depth testing for 2D rendering
		glDisable(GL_DEPTH_TEST);

		// Disable face culling (quads may be in either winding order)
		glDisable(GL_CULL_FACE);

		// Upload vertex data to GPU
		glBindBuffer(GL_ARRAY_BUFFER, vbo);
		glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(UberVertex), vertices.data(), GL_DYNAMIC_DRAW);

		// Upload index data to GPU
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_DYNAMIC_DRAW);

		// Bind shader and VAO
		shader.use();
		glBindVertexArray(vao);

		// Create projection matrix
		// If CoordinateSystem is set, use it for DPI-aware projection (logical pixels)
		// Otherwise fall back to viewport dimensions (may be incorrect on high-DPI displays)
		Foundation::Mat4 projection;
		if (coordinateSystem != nullptr) {
			projection = coordinateSystem->CreateScreenSpaceProjection();
		} else {
			projection = glm::ortho(0.0F, static_cast<float>(viewportWidth), static_cast<float>(viewportHeight), 0.0F, -1.0F, 1.0F);
		}

		glUniformMatrix4fv(projectionLoc, 1, GL_FALSE, glm::value_ptr(projection));
		// Transform is identity since transforms are baked into vertex positions at add time
		// This allows different parts of the scene to have different transforms within one batch
		Foundation::Mat4 identityTransform(1.0F);
		glUniformMatrix4fv(transformLoc, 1, GL_FALSE, glm::value_ptr(identityTransform));

		// Set viewport height and pixel ratio for clipping
		// gl_FragCoord uses physical pixels, so we need framebuffer height and pixel ratio
		float framebufferHeight = static_cast<float>(viewportHeight);
		float pixelRatio = 1.0F;
		if (coordinateSystem != nullptr) {
			// Get pixel ratio for DPI scaling
			pixelRatio = coordinateSystem->getPixelRatio();
			// Get logical height and convert to framebuffer (physical) height
			glm::vec2 windowSize = coordinateSystem->getWindowSize();
			framebufferHeight = windowSize.y * pixelRatio;
		}
		glUniform1f(viewportHeightLoc, framebufferHeight);
		glUniform1f(pixelRatioLoc, pixelRatio);

		// Bind font atlas texture (always bound, shader ignores it for shapes)
		if (fontAtlas != 0) {
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, fontAtlas);
			glUniform1i(atlasLoc, 0);
		}

		// Bind tile atlas texture and rects if provided (texture unit 1)
		int rectCount = static_cast<int>(std::min<size_t>(tileAtlasRects.size(), kMaxTileAtlasRects));
		if (tileAtlas != 0 && rectCount > 0) {
			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_2D, tileAtlas);
			if (tileAtlasLoc >= 0) {
				glUniform1i(tileAtlasLoc, 1);
			}
			if (tileAtlasRectsLoc >= 0) {
				glUniform4fv(tileAtlasRectsLoc, rectCount, reinterpret_cast<const float*>(tileAtlasRects.data()));
			}
			if (tileAtlasCountLoc >= 0) {
				glUniform1i(tileAtlasCountLoc, rectCount);
			}
		} else {
			if (tileAtlasCountLoc >= 0) {
				glUniform1i(tileAtlasCountLoc, 0);
			}
		}

		// Set instanced = false for standard batched rendering path
		glUniform1i(instancedLoc, 0);

		// Soft blend placeholder uniform (off by default)
		if (auto loc = glGetUniformLocation(shader.getProgram(), "u_softBlendMode"); loc >= 0) {
			glUniform1i(loc, 0);
		}

		// Draw
		glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indices.size()), GL_UNSIGNED_INT, nullptr);

		drawCallCount++;

		// Cleanup
		glBindVertexArray(0);
		if (fontAtlas != 0) {
			glBindTexture(GL_TEXTURE_2D, 0);
		}
		if (tileAtlas != 0 && rectCount > 0) {
			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_2D, 0);
			glActiveTexture(GL_TEXTURE0);
		}
		glDisable(GL_BLEND);

		// Accumulate stats before clearing
		frameVertexCount += vertices.size();
		frameTriangleCount += indices.size() / 3;

		// Clear buffers for next batch
		vertices.clear();
		indices.clear();
	}

	void BatchRenderer::beginFrame() {
		drawCallCount = 0;
		frameVertexCount = 0;
		frameTriangleCount = 0;
		vertices.clear();
		indices.clear();
	}

	void BatchRenderer::endFrame() {
		flush();
	}

	void BatchRenderer::setViewport(int width, int height) {
		viewportWidth = width;
		viewportHeight = height;
	}

	void BatchRenderer::getViewport(int& width, int& height) const {
		width = viewportWidth;
		height = viewportHeight;
	}

	// Sets the coordinate system to use for rendering.
	// Note: BatchRenderer does NOT take ownership of the CoordinateSystem pointer.
	// The caller is responsible for ensuring that the CoordinateSystem outlives the BatchRenderer.
	void BatchRenderer::setCoordinateSystem(CoordinateSystem* coordSystem) {
		coordinateSystem = coordSystem;
	}

	BatchRenderer::RenderStats BatchRenderer::getStats() const { // NOLINT(readability-convert-member-functions-to-static)
		RenderStats stats;
		stats.drawCalls = static_cast<uint32_t>(drawCallCount);
		stats.vertexCount = static_cast<uint32_t>(frameVertexCount);
		stats.triangleCount = static_cast<uint32_t>(frameTriangleCount);
		return stats;
	}

	void BatchRenderer::setClipBounds(const Foundation::Vec4& bounds) {
		currentClipBounds = bounds;
	}

	void BatchRenderer::clearClipBounds() {
		currentClipBounds = Foundation::Vec4(0.0F, 0.0F, 0.0F, 0.0F);
	}

	void BatchRenderer::setTransform(const Foundation::Mat4& transform) {
		currentTransform = transform;
		// Cache identity check (expensive to do per-vertex, cheap once per transform change)
		// Check all elements for true identity matrix (GLM uses column-major storage)
		transformIsIdentity =
			// Diagonal must be 1.0
			transform[0][0] == 1.0F && transform[1][1] == 1.0F && transform[2][2] == 1.0F && transform[3][3] == 1.0F &&
			// Translation (column 3) must be 0
			transform[3][0] == 0.0F && transform[3][1] == 0.0F && transform[3][2] == 0.0F &&
			// Rotation/shear off-diagonals must be 0
			transform[0][1] == 0.0F && transform[0][2] == 0.0F && transform[0][3] == 0.0F && transform[1][0] == 0.0F &&
			transform[1][2] == 0.0F && transform[1][3] == 0.0F && transform[2][0] == 0.0F && transform[2][1] == 0.0F &&
			transform[2][3] == 0.0F;
	}

	// --- GPU Instancing Methods ---

	// Maximum allowed instances to prevent excessive GPU memory allocation
	constexpr uint32_t kMaxAllowedInstances = 100000;

	InstancedMeshHandle BatchRenderer::uploadInstancedMesh(
		const renderer::TessellatedMesh& mesh,
		uint32_t						 maxInstances
	) {
		// Validate maxInstances parameter
		if (maxInstances == 0 || maxInstances > kMaxAllowedInstances) {
			std::cerr << "[BatchRenderer] Invalid maxInstances: " << maxInstances
					  << " (must be 1-" << kMaxAllowedInstances << ")" << std::endl;
			return InstancedMeshHandle{};
		}

		InstancedMeshHandle handle;
		handle.maxInstances = maxInstances;

		// Convert TessellatedMesh to InstancedMeshVertex format
		// This format is simpler: just position + color (no SDF data needed for entities)
		std::vector<InstancedMeshVertex> meshVertices;
		meshVertices.reserve(mesh.vertices.size());

		bool			  hasColors = mesh.hasColors();
		Foundation::Color defaultColor(1.0F, 1.0F, 1.0F, 1.0F);

		for (size_t i = 0; i < mesh.vertices.size(); ++i) {
			InstancedMeshVertex v;
			v.position = mesh.vertices[i];
			v.color = hasColors ? mesh.colors[i] : defaultColor;
			meshVertices.push_back(v);
		}

		// Create VAO for instanced rendering
		glGenVertexArrays(1, &handle.vao);
		glBindVertexArray(handle.vao);

		// Create mesh VBO (static geometry - uploaded once, reused for all instances)
		glGenBuffers(1, &handle.meshVBO);
		glBindBuffer(GL_ARRAY_BUFFER, handle.meshVBO);
		glBufferData(
			GL_ARRAY_BUFFER,
			static_cast<GLsizeiptr>(meshVertices.size() * sizeof(InstancedMeshVertex)),
			meshVertices.data(),
			GL_STATIC_DRAW
		);

		// Set up mesh vertex attributes (from meshVBO)
		// Location 0: position (vec2)
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(
			0, 2, GL_FLOAT, GL_FALSE, sizeof(InstancedMeshVertex),
			reinterpret_cast<void*>(offsetof(InstancedMeshVertex, position))
		);

		// Location 2: color (vec4 - Color has r,g,b,a floats)
		glEnableVertexAttribArray(2);
		glVertexAttribPointer(
			2, 4, GL_FLOAT, GL_FALSE, sizeof(InstancedMeshVertex),
			reinterpret_cast<void*>(offsetof(InstancedMeshVertex, color))
		);

		// Locations 1, 3, 4, 5 are not enabled - OpenGL provides default vertex attribute values (0,0,0,1)
		// This is fine because the instanced path only uses position and color

		// Create mesh IBO (index buffer for triangles)
		glGenBuffers(1, &handle.meshIBO);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, handle.meshIBO);
		glBufferData(
			GL_ELEMENT_ARRAY_BUFFER,
			static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(uint16_t)),
			mesh.indices.data(),
			GL_STATIC_DRAW
		);
		handle.indexCount = static_cast<uint32_t>(mesh.indices.size());
		handle.vertexCount = static_cast<uint32_t>(mesh.vertices.size());

		// Create instance VBO (dynamic - updated each frame with per-instance data)
		glGenBuffers(1, &handle.instanceVBO);
		glBindBuffer(GL_ARRAY_BUFFER, handle.instanceVBO);
		glBufferData(
			GL_ARRAY_BUFFER,
			static_cast<GLsizeiptr>(maxInstances * sizeof(InstanceData)),
			nullptr, // No initial data - will be updated each frame
			GL_DYNAMIC_DRAW
		);

		// Set up instance attributes with divisor = 1 (advance once per instance, not per vertex)
		// Location 6: instanceData1 (worldPos.xy, rotation, scale)
		// InstanceData layout: [worldPosition.x, worldPosition.y, rotation, scale, colorTint...]
		glEnableVertexAttribArray(6);
		glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, sizeof(InstanceData), reinterpret_cast<void*>(0));
		glVertexAttribDivisor(6, 1); // Key: advance once per instance

		// Location 7: instanceData2 (colorTint.rgba)
		glEnableVertexAttribArray(7);
		glVertexAttribPointer(
			7, 4, GL_FLOAT, GL_FALSE, sizeof(InstanceData),
			reinterpret_cast<void*>(offsetof(InstanceData, colorTint))
		);
		glVertexAttribDivisor(7, 1);

		glBindVertexArray(0);

		return handle;
	}

	void BatchRenderer::releaseInstancedMesh(InstancedMeshHandle& handle) {
		if (handle.vao != 0) {
			glDeleteVertexArrays(1, &handle.vao);
		}
		if (handle.meshVBO != 0) {
			glDeleteBuffers(1, &handle.meshVBO);
		}
		if (handle.meshIBO != 0) {
			glDeleteBuffers(1, &handle.meshIBO);
		}
		if (handle.instanceVBO != 0) {
			glDeleteBuffers(1, &handle.instanceVBO);
		}

		// Invalidate handle
		handle = InstancedMeshHandle{};
	}

	void BatchRenderer::drawInstanced(
		const InstancedMeshHandle& handle,
		const InstanceData*		   instances,
		uint32_t				   count,
		Foundation::Vec2		   cameraPosition,
		float					   cameraZoom,
		float					   pixelsPerMeter
	) {
		if (!handle.isValid() || count == 0 || instances == nullptr) {
			return;
		}

		// Enable blending for transparency
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_CULL_FACE);

		// Use the uber shader
		shader.use();

		// Set up projection matrix (same as flush())
		Foundation::Mat4 projection;
		if (coordinateSystem != nullptr) {
			projection = coordinateSystem->CreateScreenSpaceProjection();
		} else {
			projection = glm::ortho(
				0.0F, static_cast<float>(viewportWidth), static_cast<float>(viewportHeight), 0.0F, -1.0F, 1.0F
			);
		}
		glUniformMatrix4fv(projectionLoc, 1, GL_FALSE, glm::value_ptr(projection));

		// Identity transform (world→screen is done in shader via instancing uniforms)
		Foundation::Mat4 identity(1.0F);
		glUniformMatrix4fv(transformLoc, 1, GL_FALSE, glm::value_ptr(identity));

		// Set instancing uniforms
		glUniform1i(instancedLoc, 1); // Enable instanced rendering path
		glUniform2f(cameraPositionLoc, cameraPosition.x, cameraPosition.y);
		glUniform1f(cameraZoomLoc, cameraZoom);
		glUniform1f(pixelsPerMeterLoc, pixelsPerMeter);
		// Use logical pixels for viewport size to match the projection matrix
		// (CreateScreenSpaceProjection uses logical pixels, not physical pixels)
		float logicalWidth = static_cast<float>(viewportWidth);
		float logicalHeight = static_cast<float>(viewportHeight);
		if (coordinateSystem != nullptr) {
			glm::vec2 windowSize = coordinateSystem->getWindowSize();
			logicalWidth = windowSize.x;
			logicalHeight = windowSize.y;
		}
		glUniform2f(viewportSizeLoc, logicalWidth, logicalHeight);

		// Bind the instanced mesh VAO
		glBindVertexArray(handle.vao);
		glBindBuffer(GL_ARRAY_BUFFER, handle.instanceVBO);

		// Handle overflow by batching multiple draw calls if needed
		// This ensures all instances render even if count > maxInstances
		uint32_t remaining = count;
		uint32_t offset = 0;

		while (remaining > 0) {
			uint32_t batchSize = std::min(remaining, handle.maxInstances);

			// Upload this batch of instance data to GPU
			glBufferSubData(
				GL_ARRAY_BUFFER, 0,
				static_cast<GLsizeiptr>(batchSize * sizeof(InstanceData)),
				instances + offset
			);

			// Draw this batch of instances
			glDrawElementsInstanced(
				GL_TRIANGLES,
				static_cast<GLsizei>(handle.indexCount),
				GL_UNSIGNED_SHORT, // mesh indices are uint16_t
				nullptr,
				static_cast<GLsizei>(batchSize)
			);

			drawCallCount++;
			// Each instance renders all mesh vertices; triangle count = indices / 3
			frameVertexCount += static_cast<size_t>(handle.vertexCount) * batchSize;
			frameTriangleCount += static_cast<size_t>(handle.indexCount / 3) * batchSize;

			remaining -= batchSize;
			offset += batchSize;
		}

		glBindVertexArray(0);
		glDisable(GL_BLEND);
	}

} // namespace Renderer
