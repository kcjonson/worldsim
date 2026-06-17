// Uber Batch Renderer implementation.
// Accumulates 2D geometry (shapes + text) and renders in optimized batches.

#include "primitives/BatchRenderer.h"
#include "CoordinateSystem/CoordinateSystem.h"
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <algorithm>

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
		vertexAtlas.reserve(10000);
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

		// Get uniform locations (instanced rendering)
		cameraPositionLoc = glGetUniformLocation(shader.getProgram(), "u_cameraPosition");
		cameraZoomLoc = glGetUniformLocation(shader.getProgram(), "u_cameraZoom");
		pixelsPerMeterLoc = glGetUniformLocation(shader.getProgram(), "u_pixelsPerMeter");
		viewportSizeLoc = glGetUniformLocation(shader.getProgram(), "u_viewportSize");
		instancedLoc = glGetUniformLocation(shader.getProgram(), "u_instanced");

		// u_bakedAlpha defaults to 0.0 like all uniforms; initialize to opaque so
		// instanced draws are visible before the baked path ever sets it
		shader.use();
		glUniform1f(glGetUniformLocation(shader.getProgram(), "u_bakedAlpha"), 1.0F);
		shader.unbind();

		// Debug: verify instancing uniforms are found (only in debug builds)
#ifndef NDEBUG
		std::cerr << "[BatchRenderer] Instancing uniforms: "
				  << "u_instanced=" << instancedLoc
				  << " u_cameraPosition=" << cameraPositionLoc
				  << " u_cameraZoom=" << cameraZoomLoc
				  << " u_pixelsPerMeter=" << pixelsPerMeterLoc
				  << " u_viewportSize=" << viewportSizeLoc << std::endl;
#endif

		// Create VAO/VBO/IBO using RAII wrappers
		vao = GLVertexArray::create();
		vbo = GLBuffer::create(GL_ARRAY_BUFFER);
		ibo = GLBuffer::create(GL_ELEMENT_ARRAY_BUFFER);

		vao.bind();

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
		// Note: locations 6-7 are reserved for instancing
		glEnableVertexAttribArray(5);
		glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, sizeof(UberVertex), (void*)offsetof(UberVertex, clipBounds));

		// Bind index buffer
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);

		glBindVertexArray(0);
	}

	void BatchRenderer::shutdown() {
		// Release RAII wrappers (GPU resources freed automatically)
		vao.release();
		vbo.release();
		ibo.release();
		// Shader cleanup handled by its own RAII destructor
	}

	void BatchRenderer::recordGroup(uint32_t groupStart, float zIndex) {
		const uint32_t end = static_cast<uint32_t>(indices.size());
		if (end > groupStart) {
			drawGroups.push_back({groupStart, end - groupStart, zIndex});
			if (zIndex != 0.0F) {
				anyExplicitZ = true;
			}
		}
	}

	void BatchRenderer::addQuad( // NOLINT(readability-convert-member-functions-to-static)
		const Foundation::Rect&						bounds,
		const Foundation::Color&					fillColor,
		const std::optional<Foundation::BorderStyle>& border,
		float										cornerRadius,
		const std::optional<Foundation::LinearGradient>& gradient,
		float										zIndex
	) { // NOLINT(readability-convert-member-functions-to-static)
		const uint32_t zGroupStart = static_cast<uint32_t>(indices.size());
		uint32_t baseIndex = static_cast<uint32_t>(vertices.size());

		// Calculate rect center and half-dimensions for SDF
		float halfW = bounds.width * 0.5F;
		float halfH = bounds.height * 0.5F;

		// Per-corner fill colors (vertex order: TL, TR, BR, BL). Without a gradient
		// all four are the flat fill, matching prior behavior exactly. With one, the
		// stops are placed on the corners and the GPU interpolates across the quad.
		Foundation::Vec4 colorTL = fillColor.toVec4();
		Foundation::Vec4 colorTR = colorTL;
		Foundation::Vec4 colorBR = colorTL;
		Foundation::Vec4 colorBL = colorTL;
		if (gradient.has_value()) {
			const Foundation::Vec4 from = gradient->from.toVec4();
			const Foundation::Vec4 to = gradient->to.toVec4();
			if (gradient->horizontal) {
				colorTL = from;
				colorBL = from;
				colorTR = to;
				colorBR = to;
			} else {
				colorTL = from;
				colorTR = from;
				colorBR = to;
				colorBL = to;
			}
		}

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
			 colorTL,
			 borderData,
			 shapeParams,
			 currentClipBounds}
		);

		// Top-right corner
		vertices.push_back(
			{TransformPosition(Foundation::Vec2(centerX + expandedHalfW, centerY - expandedHalfH), currentTransform, transformIsIdentity),
			 Foundation::Vec2(expandedHalfW, -expandedHalfH), // Rect-local: top-right (expanded)
			 colorTR,
			 borderData,
			 shapeParams,
			 currentClipBounds}
		);

		// Bottom-right corner
		vertices.push_back(
			{TransformPosition(Foundation::Vec2(centerX + expandedHalfW, centerY + expandedHalfH), currentTransform, transformIsIdentity),
			 Foundation::Vec2(expandedHalfW, expandedHalfH), // Rect-local: bottom-right (expanded)
			 colorBR,
			 borderData,
			 shapeParams,
			 currentClipBounds}
		);

		// Bottom-left corner
		vertices.push_back(
			{TransformPosition(Foundation::Vec2(centerX - expandedHalfW, centerY + expandedHalfH), currentTransform, transformIsIdentity),
			 Foundation::Vec2(-expandedHalfW, expandedHalfH), // Rect-local: bottom-left (expanded)
			 colorBL,
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

		// Shapes ignore the bound atlas (shader branches on data2.w); tag 0.
		vertexAtlas.insert(vertexAtlas.end(), 4, 0);

		recordGroup(zGroupStart, zIndex);
	}

	void BatchRenderer::addShadowQuad(const Foundation::Rect& bounds, const Foundation::BoxShadow& shadow, float cornerRadius, float zIndex) {
		const uint32_t zGroupStart = static_cast<uint32_t>(indices.size());
		// The shadow's SDF shape is the rect grown by `spread`; the quad is grown a
		// further `blur` so the shader has room to fade the falloff to zero.
		const float halfW = (bounds.width * 0.5F) + shadow.spread;
		const float halfH = (bounds.height * 0.5F) + shadow.spread;
		if (halfW <= 0.0F || halfH <= 0.0F) {
			return;
		}
		const float blur = shadow.blur > 0.0F ? shadow.blur : 0.5F;
		const float cr = cornerRadius + shadow.spread;
		const float cx = bounds.x + (bounds.width * 0.5F) + shadow.offset.x;
		const float cy = bounds.y + (bounds.height * 0.5F) + shadow.offset.y;
		const float qHalfW = halfW + blur;
		const float qHalfH = halfH + blur;

		const uint32_t		   baseIndex = static_cast<uint32_t>(vertices.size());
		const Foundation::Vec4 colorVec = shadow.color.toVec4();
		const Foundation::Vec4 data1(blur, 0.0F, 0.0F, 0.0F);
		const Foundation::Vec4 data2(halfW, halfH, cr, kRenderModeShadow);

		// Corners TL, TR, BR, BL. rectLocalPos is the SDF coordinate from the shadow
		// center: the shape edge sits at +-halfSize, the falloff runs out to +-blur.
		vertices.push_back({TransformPosition(Foundation::Vec2(cx - qHalfW, cy - qHalfH), currentTransform, transformIsIdentity),
							Foundation::Vec2(-qHalfW, -qHalfH), colorVec, data1, data2, currentClipBounds});
		vertices.push_back({TransformPosition(Foundation::Vec2(cx + qHalfW, cy - qHalfH), currentTransform, transformIsIdentity),
							Foundation::Vec2(qHalfW, -qHalfH), colorVec, data1, data2, currentClipBounds});
		vertices.push_back({TransformPosition(Foundation::Vec2(cx + qHalfW, cy + qHalfH), currentTransform, transformIsIdentity),
							Foundation::Vec2(qHalfW, qHalfH), colorVec, data1, data2, currentClipBounds});
		vertices.push_back({TransformPosition(Foundation::Vec2(cx - qHalfW, cy + qHalfH), currentTransform, transformIsIdentity),
							Foundation::Vec2(-qHalfW, qHalfH), colorVec, data1, data2, currentClipBounds});

		indices.push_back(baseIndex + 0);
		indices.push_back(baseIndex + 1);
		indices.push_back(baseIndex + 2);
		indices.push_back(baseIndex + 0);
		indices.push_back(baseIndex + 2);
		indices.push_back(baseIndex + 3);

		vertexAtlas.insert(vertexAtlas.end(), 4, 0);

		recordGroup(zGroupStart, zIndex);
	}

	void BatchRenderer::addTriangles( // NOLINT(readability-convert-member-functions-to-static)
		const Foundation::Vec2*	  inputVertices,
		const uint16_t*			  inputIndices,
		size_t					  vertexCount,
		size_t					  indexCount,
		const Foundation::Color&  color,
		const Foundation::Color*  inputColors,
		float					  zIndex
	) { // NOLINT(readability-convert-member-functions-to-static)
		const uint32_t zGroupStart = static_cast<uint32_t>(indices.size());
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
				currentClipBounds // clip bounds
			});
		}

		// Add all indices (offset by baseIndex)
		for (size_t i = 0; i < indexCount; ++i) {
			indices.push_back(baseIndex + inputIndices[i]);
		}

		// Tessellated shapes ignore the bound atlas; tag every vertex 0.
		vertexAtlas.insert(vertexAtlas.end(), vertexCount, 0);

		recordGroup(zGroupStart, zIndex);
	}

	void BatchRenderer::addTextQuad(
		const Foundation::Vec2&	 position,
		const Foundation::Vec2&	 size,
		const Foundation::Vec2&	 uvMin,
		const Foundation::Vec2&	 uvMax,
		const Foundation::Color& color,
		GLuint					 atlasTexture,
		float					 zIndex
	) {
		const uint32_t zGroupStart = static_cast<uint32_t>(indices.size());
		// Resolve the atlas this glyph samples. 0 means "use the default atlas"
		// (Roboto, set via setFontAtlas), so existing callers are unchanged.
		GLuint resolvedAtlas = (atlasTexture != 0) ? atlasTexture : defaultFontAtlas;

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

		// Tag all 4 vertices with the atlas they sample, so flush() can group
		// this glyph into the matching per-atlas draw run.
		vertexAtlas.insert(vertexAtlas.end(), 4, resolvedAtlas);

		recordGroup(zGroupStart, zIndex);
	}

	void BatchRenderer::setFontAtlas(GLuint atlasTexture, float pixelRange) {
		defaultFontAtlas = atlasTexture;
		fontPixelRange = pixelRange;
	}

	void BatchRenderer::flush() {
		if (vertices.empty()) {
			return;
		}

		// Resolve draw order. Submission ("organic") order is kept unless some draw
		// carried an explicit (non-zero) z; then the per-draw-call groups are
		// stable-sorted by z and the emit-order index list is rebuilt (groups, not
		// triangles). The zIndex came from the component layer; the renderer only
		// orders by it. The fast path costs nothing beyond the group records.
		const std::vector<uint32_t>* emit = &indices;
		std::vector<uint32_t>		 sortedIndices;
		if (anyExplicitZ && !drawGroups.empty()) {
			std::stable_sort(drawGroups.begin(), drawGroups.end(), [](const DrawGroup& a, const DrawGroup& b) { return a.zIndex < b.zIndex; });
			sortedIndices.reserve(indices.size());
			for (const DrawGroup& g : drawGroups) {
				sortedIndices.insert(sortedIndices.end(), indices.begin() + g.indexStart, indices.begin() + g.indexStart + g.indexCount);
			}
			emit = &sortedIndices;
		}
		const std::vector<uint32_t>& idx = *emit;

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
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(uint32_t), idx.data(), GL_DYNAMIC_DRAW);

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

		// Set instanced = false for standard batched rendering path
		glUniform1i(instancedLoc, 0);

		glActiveTexture(GL_TEXTURE0);
		glUniform1i(atlasLoc, 0);

		// Multi-atlas draw splitting.
		//
		// All shapes and text share one VBO/IBO and one draw order (z-order ==
		// submission order). Text vertices are tagged in `vertexAtlas` with the
		// MSDF atlas they sample; shapes are tagged 0 (they ignore the bound
		// texture, the shader branches on data2.w). We walk triangles in order,
		// keeping a contiguous run of indices, and start a NEW draw call only
		// when a text triangle needs a different atlas than the one currently
		// bound. Triangles are never reordered, so z-order is exact.
		//
		// Common case (all text from one atlas, e.g. the default Roboto): every
		// text triangle needs the same atlas, so this collapses to a single
		// glDrawElements identical to the prior single-atlas behavior.
		const size_t indexCount = idx.size();
		GLuint		 boundAtlas = defaultFontAtlas; // What's currently bound on the GPU
		if (boundAtlas != 0) {
			glBindTexture(GL_TEXTURE_2D, boundAtlas);
		}

		size_t runStart = 0; // First index of the current contiguous run
		for (size_t tri = 0; tri < indexCount; tri += 3) {
			// A triangle's vertices are all shape (tag 0) or all the same text
			// atlas; max() yields the text atlas if any, else 0 (no requirement).
			GLuint required = vertexAtlas[idx[tri]];
			required = std::max(required, vertexAtlas[idx[tri + 1]]);
			required = std::max(required, vertexAtlas[idx[tri + 2]]);

			if (required != 0 && required != boundAtlas) {
				// Flush the run accumulated so far with the previously bound atlas.
				if (tri > runStart) {
					glDrawElements(
						GL_TRIANGLES,
						static_cast<GLsizei>(tri - runStart),
						GL_UNSIGNED_INT,
						(const void*)(runStart * sizeof(uint32_t))
					);
					drawCallCount++;
				}
				// Bind the atlas this run needs and start a new run here.
				boundAtlas = required;
				glBindTexture(GL_TEXTURE_2D, boundAtlas);
				runStart = tri;
			}
		}

		// Draw the final run.
		if (indexCount > runStart) {
			glDrawElements(
				GL_TRIANGLES,
				static_cast<GLsizei>(indexCount - runStart),
				GL_UNSIGNED_INT,
				(const void*)(runStart * sizeof(uint32_t))
			);
			drawCallCount++;
		}

		// Cleanup
		glBindVertexArray(0);
		glBindTexture(GL_TEXTURE_2D, 0);
		glDisable(GL_BLEND);

		// Accumulate stats before clearing
		frameVertexCount += vertices.size();
		frameTriangleCount += indices.size() / 3;

		// Clear buffers for next batch
		vertices.clear();
		indices.clear();
		vertexAtlas.clear();
		drawGroups.clear();
		anyExplicitZ = false;
	}

	void BatchRenderer::beginFrame() {
		drawCallCount = 0;
		frameVertexCount = 0;
		frameTriangleCount = 0;
		vertices.clear();
		indices.clear();
		vertexAtlas.clear();
		drawGroups.clear();
		anyExplicitZ = false;
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

		// Create VAO for instanced rendering (RAII wrapper)
		handle.vao = GLVertexArray::create();
		handle.vao.bind();

		// Create mesh VBO (static geometry - uploaded once, reused for all instances)
		handle.meshVBO = GLBuffer::create(GL_ARRAY_BUFFER);
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

		// Create mesh IBO (index buffer for triangles) - RAII wrapper
		handle.meshIBO = GLBuffer::create(GL_ELEMENT_ARRAY_BUFFER);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, handle.meshIBO);
		glBufferData(
			GL_ELEMENT_ARRAY_BUFFER,
			static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(uint16_t)),
			mesh.indices.data(),
			GL_STATIC_DRAW
		);
		handle.indexCount = static_cast<uint32_t>(mesh.indices.size());
		handle.vertexCount = static_cast<uint32_t>(mesh.vertices.size());

		// Create instance VBO (dynamic - updated each frame with per-instance data) - RAII wrapper
		handle.instanceVBO = GLBuffer::create(GL_ARRAY_BUFFER);
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
		// RAII wrappers automatically release GPU resources when destroyed or reset
		handle.vao.release();
		handle.meshVBO.release();
		handle.meshIBO.release();
		handle.instanceVBO.release();

		// Reset other fields
		handle.indexCount = 0;
		handle.vertexCount = 0;
		handle.maxInstances = 0;
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

		// Bind the instanced mesh VAO and instance buffer
		handle.vao.bind();
		handle.instanceVBO.bind();

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
