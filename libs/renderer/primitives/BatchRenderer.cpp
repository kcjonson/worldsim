// Uber Batch Renderer implementation.
// Accumulates 2D geometry (shapes + text) and renders in optimized batches.

#include "primitives/BatchRenderer.h"
#include "CoordinateSystem/CoordinateSystem.h"
#include <glm/gtc/type_ptr.hpp>
#include <iostream>

namespace Renderer {

	namespace {
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

		// Get uniform locations
		projectionLoc = glGetUniformLocation(shader.getProgram(), "u_projection");
		transformLoc = glGetUniformLocation(shader.getProgram(), "u_transform");
		atlasLoc = glGetUniformLocation(shader.getProgram(), "u_atlas");
		viewportHeightLoc = glGetUniformLocation(shader.getProgram(), "u_viewportHeight");
		pixelRatioLoc = glGetUniformLocation(shader.getProgram(), "u_pixelRatio");

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
		// Top-left corner
		vertices.push_back(
			{TransformPosition(Foundation::Vec2(centerX - expandedHalfW, centerY - expandedHalfH), currentTransform, transformIsIdentity),
			 Foundation::Vec2(-expandedHalfW, -expandedHalfH), // Rect-local: top-left (expanded)
			 colorVec,
			 borderData,
			 shapeParams,
			 currentClipBounds}
		);

		// Top-right corner
		vertices.push_back(
			{TransformPosition(Foundation::Vec2(centerX + expandedHalfW, centerY - expandedHalfH), currentTransform, transformIsIdentity),
			 Foundation::Vec2(expandedHalfW, -expandedHalfH), // Rect-local: top-right (expanded)
			 colorVec,
			 borderData,
			 shapeParams,
			 currentClipBounds}
		);

		// Bottom-right corner
		vertices.push_back(
			{TransformPosition(Foundation::Vec2(centerX + expandedHalfW, centerY + expandedHalfH), currentTransform, transformIsIdentity),
			 Foundation::Vec2(expandedHalfW, expandedHalfH), // Rect-local: bottom-right (expanded)
			 colorVec,
			 borderData,
			 shapeParams,
			 currentClipBounds}
		);

		// Bottom-left corner
		vertices.push_back(
			{TransformPosition(Foundation::Vec2(centerX - expandedHalfW, centerY + expandedHalfH), currentTransform, transformIsIdentity),
			 Foundation::Vec2(-expandedHalfW, expandedHalfH), // Rect-local: bottom-left (expanded)
			 colorVec,
			 borderData,
			 shapeParams,
			 currentClipBounds}
		);

		// Add 6 indices (2 triangles)
		indices.push_back(baseIndex + 0);
		indices.push_back(baseIndex + 1);
		indices.push_back(baseIndex + 2);

		indices.push_back(baseIndex + 0);
		indices.push_back(baseIndex + 2);
		indices.push_back(baseIndex + 3);
	}

	void BatchRenderer::addTriangles( // NOLINT(readability-convert-member-functions-to-static)
		const Foundation::Vec2*	 inputVertices,
		const uint16_t*			 inputIndices,
		size_t					 vertexCount,
		size_t					 indexCount,
		const Foundation::Color& color
	) { // NOLINT(readability-convert-member-functions-to-static)
		uint32_t baseIndex = static_cast<uint32_t>(vertices.size());

		Foundation::Vec4 colorVec = color.toVec4();

		// Default data (not used for tessellated shapes, but required for vertex format)
		// Use borderPosition=1 (Center) so shader treats these as shapes, not text
		Foundation::Vec2 zeroVec2(0.0F, 0.0F);
		Foundation::Vec4 zeroVec4(0.0F, 0.0F, 0.0F, 0.0F);
		Foundation::Vec4 shapeParams(0.0F, 0.0F, 0.0F, 1.0F); // borderPos=1 marks as shape

		// Add all vertices (positions transformed by currentTransform)
		for (size_t i = 0; i < vertexCount; ++i) {
			vertices.push_back({
				TransformPosition(inputVertices[i], currentTransform, transformIsIdentity),
				zeroVec2, // texCoord (unused for triangles)
				colorVec,
				zeroVec4,		  // data1 (unused)
				shapeParams,	  // data2 with borderPos >= 0 marks as shape
				currentClipBounds // clip bounds
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
			 currentClipBounds}
		);

		// Top-right
		vertices.push_back(
			{TransformPosition(Foundation::Vec2(position.x + size.x, position.y), currentTransform, transformIsIdentity),
			 Foundation::Vec2(uvMax.x, uvMax.y), // UV flipped
			 colorVec,
			 zeroVec4,
			 textParams,
			 currentClipBounds}
		);

		// Bottom-right
		vertices.push_back(
			{TransformPosition(Foundation::Vec2(position.x + size.x, position.y + size.y), currentTransform, transformIsIdentity),
			 Foundation::Vec2(uvMax.x, uvMin.y), // UV flipped
			 colorVec,
			 zeroVec4,
			 textParams,
			 currentClipBounds}
		);

		// Bottom-left
		vertices.push_back(
			{TransformPosition(Foundation::Vec2(position.x, position.y + size.y), currentTransform, transformIsIdentity),
			 Foundation::Vec2(uvMin.x, uvMin.y), // UV flipped
			 colorVec,
			 zeroVec4,
			 textParams,
			 currentClipBounds}
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

		// Draw
		glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indices.size()), GL_UNSIGNED_INT, nullptr);

		drawCallCount++;

		// Cleanup
		glBindVertexArray(0);
		if (fontAtlas != 0) {
			glBindTexture(GL_TEXTURE_2D, 0);
		}
		glDisable(GL_BLEND);

		// Clear buffers for next batch
		vertices.clear();
		indices.clear();
	}

	void BatchRenderer::beginFrame() {
		drawCallCount = 0;
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
		stats.vertexCount = static_cast<uint32_t>(vertices.size());
		stats.triangleCount = static_cast<uint32_t>(indices.size() / 3);
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
		// Check all relevant elements for 2D affine transforms
		transformIsIdentity =
			(transform[0][0] == 1.0F && transform[1][1] == 1.0F && transform[2][2] == 1.0F && transform[3][3] == 1.0F &&
			 transform[3][0] == 0.0F && transform[3][1] == 0.0F && transform[3][2] == 0.0F && transform[0][1] == 0.0F &&
			 transform[1][0] == 0.0F);
	}

} // namespace Renderer
