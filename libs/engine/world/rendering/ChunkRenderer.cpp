#include "ChunkRenderer.h"

#include <debug/Tunables.h>
#include <primitives/BatchRenderer.h>
#include <primitives/Primitives.h>
#include <utils/Log.h>

#include <GL/glew.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <vector>

namespace engine::world {

	namespace {
		// The tile-data texture is uploaded as raw TileRenderData; the fragment
		// shader unpacks bytes from uint texels assuming this exact layout.
		static_assert(sizeof(TileRenderData) == 16, "tile.frag texel layout must match TileRenderData");
		static_assert(sizeof(HalfTexel) == 6, "RGB16F upload expects packed half texels");
		static_assert(sizeof(ByteTexel) == 4, "RGBA8 upload expects packed byte texels");

		using Field = TerrainDistanceField;

		// Near tiles per atlas row: the atlas is at most 32 * 34 = 1088 texels wide.
		constexpr int kNearAtlasMaxColumns = Field::kTilesPerSide;

		// Texture units: atlas 0, tile data 1, then the distance field.
		constexpr int kUnitTileAtlas	= 0;
		constexpr int kUnitTileData		= 1;
		constexpr int kUnitSdfTileMap	= 2;
		constexpr int kUnitSdfNear		= 3;
		constexpr int kUnitSdfFar		= 4;
		constexpr int kUnitShoreProfile = 5;
		constexpr int kUnitChannelFrame = 6;

		// Wraps shader time so its float precision holds; the animations jump once an hour.
		constexpr double kTimeWrapSeconds = 3600.0;

		/// A debug-server tunable (10.5) and the shader uniform it drives.
		struct WaterTunable {
			const char*			 name;
			const char*			 uniform;
			std::array<float, 3> value;
			size_t				 count;
		};

		// Starting values from terrain-polygons-architecture.md 10.5; the ones 10.5
		// leaves open (colors, patch thresholds, shimmer, foam motion, flow speed,
		// wetland mud gain) are this task's first pass. Widths are meters.
		constexpr std::array kWaterTunables = {
			WaterTunable{"terrain/shore/wobbleAmp", "u_wobbleAmp", {0.12F}, 1},
			WaterTunable{"terrain/shore/wobbleWavelength", "u_wobbleWavelength", {1.6F}, 1},
			WaterTunable{"terrain/shore/alongAmp", "u_alongAmp", {0.35F}, 1},
			WaterTunable{"terrain/shore/alongWavelength", "u_alongWavelength", {6.0F}, 1},
			WaterTunable{"terrain/shore/drySandW", "u_drySandW", {1.6F}, 1},
			WaterTunable{"terrain/shore/wetSandW", "u_wetSandW", {0.9F}, 1},
			WaterTunable{"terrain/shore/mudW", "u_mudW", {1.1F}, 1},
			WaterTunable{"terrain/shore/bandTail", "u_bandTail", {0.65F}, 1},
			WaterTunable{"terrain/shore/wetlandMudGain", "u_wetlandMudGain", {1.8F}, 1},
			WaterTunable{"terrain/shore/edgeW", "u_edgeW", {0.12F}, 1},
			WaterTunable{"terrain/shore/edgeSlopeGain", "u_edgeSlopeGain", {2.5F}, 1},
			WaterTunable{"terrain/shore/edgeMin", "u_edgeMin", {0.15F}, 1},
			WaterTunable{"terrain/shore/edgeMax", "u_edgeMax", {0.85F}, 1},
			// 10.5 says 0.5, which leaves zoom 0.25 (exactly 0.5 m a pixel) on the fine
			// path; 10.2 means 2 px/m to take the far one, where patches only speckle.
			WaterTunable{"terrain/shore/lodBandM", "u_lodBandM", {0.4F}, 1},

			WaterTunable{"terrain/water/shallowsK", "u_shallowsK", {0.9F}, 1},
			WaterTunable{"terrain/water/shallowsMin", "u_shallowsMin", {0.5F}, 1},
			WaterTunable{"terrain/water/shallowsMax", "u_shallowsMax", {6.0F}, 1},
			WaterTunable{"terrain/water/slopeMin", "u_slopeMin", {0.15F}, 1},
			WaterTunable{"terrain/water/midAt", "u_midAt", {0.45F}, 1},
			WaterTunable{"terrain/water/bedOpacity", "u_bedOpacity", {0.5F}, 1},
			WaterTunable{"terrain/water/bedFade", "u_bedFade", {0.35F}, 1},
			WaterTunable{"terrain/water/patchWavelength", "u_patchWavelength", {7.0F}, 1},
			// 10.5 says 0.45; at that amplitude open water read as camouflage, not a bottom.
			WaterTunable{"terrain/water/patchAmp", "u_patchAmp", {0.2F}, 1},
			WaterTunable{"terrain/water/barT0", "u_barT0", {0.3F}, 1},
			WaterTunable{"terrain/water/barT1", "u_barT1", {0.6F}, 1},
			WaterTunable{"terrain/water/weedT0", "u_weedT0", {0.3F}, 1},
			WaterTunable{"terrain/water/weedT1", "u_weedT1", {0.6F}, 1},
			WaterTunable{"terrain/water/shimmerAmp", "u_shimmerAmp", {0.025F}, 1},
			WaterTunable{"terrain/water/shimmerFreq", "u_shimmerFreq", {0.9F}, 1},
			WaterTunable{"terrain/water/shimmerDrift", "u_shimmerDrift", {0.25F}, 1},
			WaterTunable{"terrain/water/foamW", "u_foamW", {0.4F}, 1},
			WaterTunable{"terrain/water/foamWavelength", "u_foamWavelength", {1.2F}, 1},
			WaterTunable{"terrain/water/foamDrift", "u_foamDrift", {0.3F}, 1},

			WaterTunable{"terrain/river/thalwegInner", "u_thalwegInner", {0.6F}, 1},
			WaterTunable{"terrain/river/thalwegOuter", "u_thalwegOuter", {1.1F}, 1},
			WaterTunable{"terrain/river/thalwegDepth", "u_thalwegDepth", {0.9F}, 1},
			// 10.5 gives the riffle frequency as 2 pi / 1.5 m; the shader snaps the wavelength to tile the 64 m arc wrap.
			WaterTunable{"terrain/river/riffleWavelength", "u_riffleWavelength", {1.5F}, 1},
			WaterTunable{"terrain/river/riffleAmp", "u_riffleAmp", {0.3F}, 1},
			WaterTunable{"terrain/river/poolAmp", "u_poolAmp", {0.35F}, 1},
			WaterTunable{"terrain/river/flowSpeed", "u_flowSpeed", {0.6F}, 1},

			WaterTunable{"terrain/color/drySand", "u_drySand", {0.84F, 0.75F, 0.53F}, 3},
			WaterTunable{"terrain/color/wetSand", "u_wetSand", {0.62F, 0.53F, 0.37F}, 3},
			WaterTunable{"terrain/color/mudBank", "u_mudBank", {0.36F, 0.28F, 0.18F}, 3},
			WaterTunable{"terrain/color/bedGrass", "u_bedGrass", {0.30F, 0.38F, 0.24F}, 3},
			WaterTunable{"terrain/color/edge", "u_edgeColor", {0.16F, 0.13F, 0.09F}, 3},
			WaterTunable{"terrain/color/waterShallow", "u_waterShallow", {0.42F, 0.64F, 0.64F}, 3},
			WaterTunable{"terrain/color/waterMid", "u_waterMid", {0.16F, 0.44F, 0.55F}, 3},
			WaterTunable{"terrain/color/waterDeep", "u_waterDeep", {0.07F, 0.24F, 0.42F}, 3},
			WaterTunable{"terrain/color/riffleLight", "u_riffleLight", {0.70F, 0.84F, 0.86F}, 3},
			WaterTunable{"terrain/color/bar", "u_barColor", {0.62F, 0.66F, 0.52F}, 3},
			WaterTunable{"terrain/color/weed", "u_weedColor", {0.20F, 0.36F, 0.26F}, 3},
			WaterTunable{"terrain/color/foam", "u_foam", {0.92F, 0.95F, 0.95F}, 3},
		};

		Renderer::GLTexture makeTexture(int width, int height, GLenum internalFormat, GLenum format, GLenum type, const void* data, GLint filter) {
			Renderer::GLTexture texture(width, height, internalFormat, format, type, data);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
			return texture;
		}

		void bindUnit(int unit, GLuint texture) {
			glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + unit));
			glBindTexture(GL_TEXTURE_2D, texture);
		}
	} // namespace

	ChunkRenderer::ChunkRenderer(float pixelsPerMeter)
		: pixelsPerMeterValue(pixelsPerMeter) {}

	bool ChunkRenderer::initGL() {
		if (glInitAttempted) {
			return shader.IsValid();
		}
		glInitAttempted = true;

		if (!shader.LoadFromFile("tile.vert", "tile.frag")) {
			LOG_ERROR(Renderer, "ChunkRenderer: failed to load tile shader");
			return false;
		}

		GLuint program = shader.getProgram();
		loc.projection = glGetUniformLocation(program, "u_projection");
		loc.chunkOrigin = glGetUniformLocation(program, "u_chunkOrigin");
		loc.chunkWorldSize = glGetUniformLocation(program, "u_chunkWorldSize");
		loc.chunkTileOrigin = glGetUniformLocation(program, "u_chunkTileOrigin");
		loc.cameraPos = glGetUniformLocation(program, "u_cameraPos");
		loc.cameraZoom = glGetUniformLocation(program, "u_cameraZoom");
		loc.pixelsPerMeter = glGetUniformLocation(program, "u_pixelsPerMeter");
		loc.viewportSize = glGetUniformLocation(program, "u_viewportSize");
		loc.tileData = glGetUniformLocation(program, "u_tileData");
		loc.tileAtlas = glGetUniformLocation(program, "u_tileAtlas");
		loc.tileAtlasRectCount = glGetUniformLocation(program, "u_tileAtlasRectCount");
		loc.tileAtlasRects = glGetUniformLocation(program, "u_tileAtlasRects");
		loc.hasWater = glGetUniformLocation(program, "u_hasWater");
		loc.sdfTileMap = glGetUniformLocation(program, "u_sdfTileMap");
		loc.sdfNear = glGetUniformLocation(program, "u_sdfNear");
		loc.sdfNearColumns = glGetUniformLocation(program, "u_sdfNearColumns");
		loc.sdfFar = glGetUniformLocation(program, "u_sdfFar");
		loc.shoreProfile = glGetUniformLocation(program, "u_shoreProfile");
		loc.channelFrame = glGetUniformLocation(program, "u_channelFrame");
		loc.time = glGetUniformLocation(program, "u_time");
		loc.metersPerPixel = glGetUniformLocation(program, "u_metersPerPixel");

		Foundation::Tunables& tunables = Foundation::Tunables::instance();
		tunableUniforms.clear();
		tunableUniforms.reserve(kWaterTunables.size());
		for (const WaterTunable& t : kWaterTunables) {
			const float* values = tunables.add(t.name, std::span<const float>(t.value.data(), t.count));
			const int	 location = glGetUniformLocation(program, t.uniform);
			if (location < 0) {
				LOG_WARNING(Renderer, "ChunkRenderer: tunable uniform %s is not active in tile.frag", t.uniform);
			}
			tunableUniforms.push_back({location, values, t.count});
		}

		// Unit quad as triangle strip: (0,0) (1,0) (0,1) (1,1)
		constexpr float kQuad[] = {0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F};
		quadVAO = Renderer::GLVertexArray::create();
		quadVBO = Renderer::GLBuffer::create(GL_ARRAY_BUFFER);
		quadVAO.bind();
		quadVBO.bind();
		glBufferData(GL_ARRAY_BUFFER, sizeof(kQuad), kQuad, GL_STATIC_DRAW);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
		Renderer::GLVertexArray::unbind();

		GLint previousAlignment = 4;
		glGetIntegerv(GL_UNPACK_ALIGNMENT, &previousAlignment);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
		const uint16_t	noNearTile = Field::kNoNearTile;
		const HalfTexel landTexel  = Field::kLandSdfTexel;
		const HalfTexel zeroHalf{};
		const ByteTexel zeroByte{};
		defaultTileMap = makeTexture(1, 1, GL_R16UI, GL_RED_INTEGER, GL_UNSIGNED_SHORT, &noNearTile, GL_NEAREST);
		defaultFar	   = makeTexture(1, 1, GL_RGB16F, GL_RGB, GL_HALF_FLOAT, &landTexel, GL_NEAREST);
		defaultProfile = makeTexture(1, 1, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, &zeroByte, GL_NEAREST);
		defaultFrame   = makeTexture(1, 1, GL_RGB16F, GL_RGB, GL_HALF_FLOAT, &zeroHalf, GL_NEAREST);
		glPixelStorei(GL_UNPACK_ALIGNMENT, previousAlignment);

		return true;
	}

	bool ChunkRenderer::takeStaleBudget(size_t bytes) {
		if (staleBytesThisFrame > 0 && staleBytesThisFrame + bytes > kStaleReuploadBudgetBytes) {
			return false;
		}
		staleBytesThisFrame += std::max<size_t>(bytes, 1);
		return true;
	}

	size_t ChunkRenderer::uploadDistanceField(ChunkTextures& entry, const TerrainDistanceField& field) {
		entry.sdfTileMap.release();
		entry.sdfNear.release();
		entry.sdfFar.release();
		entry.shoreProfile.release();
		entry.channelFrame.release();
		entry.nearColumns = 1;
		entry.waterBytes  = 0;

		const size_t nearTiles = field.nearTileCount();
		entry.hasWater		   = nearTiles > 0 || !field.farTexels.empty();
		if (!entry.hasWater) {
			return 0;
		}

		// RGB16F rows are 6 bytes a texel, so they're only 2-byte aligned.
		GLint previousAlignment = 4;
		glGetIntegerv(GL_UNPACK_ALIGNMENT, &previousAlignment);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 2);

		size_t bytes = 0;
		if (nearTiles > 0) {
			// Tile i sits in atlas cell (i % columns, i / columns); the tile map's
			// near-tile indices are the cell indices as they stand.
			const int columns = std::min(static_cast<int>(nearTiles), kNearAtlasMaxColumns);
			const int rows	  = static_cast<int>((nearTiles + static_cast<size_t>(columns) - 1) / static_cast<size_t>(columns));
			const int stride  = Field::kNearTileStride;
			const int width	  = columns * stride;
			const int height  = rows * stride;
			std::vector<HalfTexel> atlas(static_cast<size_t>(width) * static_cast<size_t>(height), Field::kLandSdfTexel);
			for (size_t tile = 0; tile < nearTiles; ++tile) {
				const int cellX = static_cast<int>(tile % static_cast<size_t>(columns)) * stride;
				const int cellY = static_cast<int>(tile / static_cast<size_t>(columns)) * stride;
				const HalfTexel* src = field.nearTexels.data() + tile * Field::kNearTileTexelCount;
				for (int v = 0; v < stride; ++v) {
					std::copy_n(
						src + static_cast<size_t>(v) * static_cast<size_t>(stride),
						stride,
						atlas.begin() + static_cast<ptrdiff_t>(static_cast<size_t>(cellY + v) * static_cast<size_t>(width) + static_cast<size_t>(cellX))
					);
				}
			}
			entry.sdfNear	  = makeTexture(width, height, GL_RGB16F, GL_RGB, GL_HALF_FLOAT, atlas.data(), GL_LINEAR);
			entry.nearColumns = columns;
			entry.sdfTileMap  = makeTexture(
				 Field::kTilesPerSide, Field::kTilesPerSide, GL_R16UI, GL_RED_INTEGER, GL_UNSIGNED_SHORT, field.tileMap.data(), GL_NEAREST
			 );
			bytes += atlas.size() * sizeof(HalfTexel) + field.tileMap.size() * sizeof(uint16_t);
		}
		if (!field.farTexels.empty()) {
			entry.sdfFar = makeTexture(Field::kFarStride, Field::kFarStride, GL_RGB16F, GL_RGB, GL_HALF_FLOAT, field.farTexels.data(), GL_LINEAR);
			bytes += field.farBytes();
		}
		if (!field.shoreProfile.empty()) {
			entry.shoreProfile =
				makeTexture(Field::kDetailStride, Field::kDetailStride, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, field.shoreProfile.data(), GL_LINEAR);
			bytes += field.shoreProfileBytes();
		}
		if (!field.channelFrame.empty()) {
			// The shader fetches and unwraps the four texels itself (arc length wraps).
			entry.channelFrame =
				makeTexture(Field::kDetailStride, Field::kDetailStride, GL_RGB16F, GL_RGB, GL_HALF_FLOAT, field.channelFrame.data(), GL_NEAREST);
			bytes += field.channelFrameBytes();
		}

		glPixelStorei(GL_UNPACK_ALIGNMENT, previousAlignment);
		entry.waterBytes = bytes;
		return bytes;
	}

	ChunkRenderer::ChunkTextures& ChunkRenderer::ensureTextures(const Chunk& chunk) {
		ChunkTextures& entry = textureCache[chunk.coordinate()];
		entry.lastAccessFrame = frameCounter;

		glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + kUnitTileData));
		const uint32_t tileVersion = chunk.renderDataVersion();
		if (!entry.tileData.isValid()) {
			entry.tileData = makeTexture(kChunkSize, kChunkSize, GL_RGBA32UI, GL_RGBA_INTEGER, GL_UNSIGNED_INT, chunk.renderData(), GL_NEAREST);
			entry.tileVersion = tileVersion;
		} else if (entry.tileVersion != tileVersion && takeStaleBudget(kTileDataBytes)) {
			entry.tileData.bind();
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kChunkSize, kChunkSize, GL_RGBA_INTEGER, GL_UNSIGNED_INT, chunk.renderData());
			entry.tileVersion = tileVersion;
		}

		const TerrainDistanceField& field = chunk.terrainDistanceField();
		if (!entry.waterUploaded || entry.waterVersion != field.version) {
			const size_t bytes = field.nearBytes() + field.farBytes() + field.shoreProfileBytes() + field.channelFrameBytes();
			if (!entry.waterUploaded || takeStaleBudget(bytes)) {
				const size_t uploaded = uploadDistanceField(entry, field);
				entry.waterUploaded	  = true;
				entry.waterVersion	  = field.version;
				if (entry.hasWater) {
					LOG_DEBUG(
						Renderer,
						"ChunkRenderer: chunk (%d,%d) distance field v%u: %zu B (near %zu tiles, far %zu, profile %zu, frame %zu); cache tile %zu B, water %zu B",
						chunk.coordinate().x,
						chunk.coordinate().y,
						field.version,
						uploaded,
						field.nearTileCount(),
						field.farBytes(),
						field.shoreProfileBytes(),
						field.channelFrameBytes(),
						cachedTileBytes(),
						cachedWaterBytes()
					);
				}
			}
		}
		return entry;
	}

	void ChunkRenderer::bindWater(const ChunkTextures& entry) const {
		glUniform1i(loc.hasWater, entry.hasWater ? 1 : 0);
		if (!entry.hasWater) {
			return;
		}
		auto pick = [](const Renderer::GLTexture& texture, const Renderer::GLTexture& fallback) {
			return texture.isValid() ? texture.handle() : fallback.handle();
		};
		bindUnit(kUnitSdfTileMap, pick(entry.sdfTileMap, defaultTileMap));
		bindUnit(kUnitSdfNear, pick(entry.sdfNear, defaultFar));
		bindUnit(kUnitSdfFar, pick(entry.sdfFar, defaultFar));
		bindUnit(kUnitShoreProfile, pick(entry.shoreProfile, defaultProfile));
		bindUnit(kUnitChannelFrame, pick(entry.channelFrame, defaultFrame));
		glUniform1i(loc.sdfNearColumns, entry.nearColumns);
	}

	size_t ChunkRenderer::cachedTileBytes() const {
		size_t total = 0;
		for (const auto& [coord, entry] : textureCache) {
			total += entry.tileData.isValid() ? kTileDataBytes : 0;
		}
		return total;
	}

	size_t ChunkRenderer::cachedWaterBytes() const {
		size_t total = 0;
		for (const auto& [coord, entry] : textureCache) {
			total += entry.waterBytes;
		}
		return total;
	}

	void ChunkRenderer::evictStaleTextures() {
		if (textureCache.size() <= kMaxCachedTextures) {
			return;
		}

		std::vector<std::pair<ChunkCoordinate, uint64_t>> byAge;
		byAge.reserve(textureCache.size());
		for (const auto& [coord, entry] : textureCache) {
			if (entry.lastAccessFrame != frameCounter) { // never evict chunks drawn this frame
				byAge.emplace_back(coord, entry.lastAccessFrame);
			}
		}
		std::sort(byAge.begin(), byAge.end(), [](const auto& a, const auto& b) { return a.second < b.second; });

		size_t toEvict = std::min(byAge.size(), kEvictionBatchSize);
		for (size_t i = 0; i < toEvict; ++i) {
			textureCache.erase(byAge[i].first);
		}
	}

	void ChunkRenderer::applyTunables() const {
		for (const TunableUniform& t : tunableUniforms) {
			if (t.location < 0) {
				continue;
			}
			if (t.count == 3) {
				glUniform3fv(t.location, 1, t.values);
			} else {
				glUniform1f(t.location, t.values[0]);
			}
		}
	}

	void ChunkRenderer::render(const ChunkManager& chunkManager, const WorldCamera& camera, int viewportWidth, int viewportHeight) {
		lastTiles = 0;
		lastChunks = 0;
		staleBytesThisFrame = 0;
		frameCounter++;

		Foundation::Rect visibleRect = camera.getVisibleRect(viewportWidth, viewportHeight, pixelsPerMeterValue);
		auto [minCorner, maxCorner] = camera.getVisibleCorners(viewportWidth, viewportHeight, pixelsPerMeterValue);
		std::vector<const Chunk*> visibleChunks = chunkManager.getVisibleChunks(minCorner, maxCorner);
		if (visibleChunks.empty()) {
			return;
		}

		if (!initGL()) {
			return;
		}

		// Flush any pending batched geometry so draw order stays correct
		auto* batchRenderer = Renderer::Primitives::getBatchRenderer();
		if (batchRenderer != nullptr) {
			batchRenderer->flush();
		}

		// Save GL state before modifying (mirrors EntityRenderer's baked pass)
		GLboolean blendEnabled = glIsEnabled(GL_BLEND);
		GLboolean depthTestEnabled = glIsEnabled(GL_DEPTH_TEST);
		GLboolean cullFaceEnabled = glIsEnabled(GL_CULL_FACE);

		glDisable(GL_BLEND); // tiles are opaque and fill their quad
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_CULL_FACE);

		shader.use();

		glm::mat4 projection =
			glm::ortho(0.0F, static_cast<float>(viewportWidth), static_cast<float>(viewportHeight), 0.0F, -1.0F, 1.0F);
		glUniformMatrix4fv(loc.projection, 1, GL_FALSE, glm::value_ptr(projection));
		glUniform2f(loc.cameraPos, camera.position().x, camera.position().y);
		glUniform1f(loc.cameraZoom, camera.zoom());
		glUniform1f(loc.pixelsPerMeter, pixelsPerMeterValue);
		glUniform2f(loc.viewportSize, static_cast<float>(viewportWidth), static_cast<float>(viewportHeight));
		glUniform1f(loc.metersPerPixel, 1.0F / (pixelsPerMeterValue * camera.zoom()));
		const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime).count();
		glUniform1f(loc.time, static_cast<float>(std::fmod(seconds, kTimeWrapSeconds)));
		applyTunables();

		const auto& atlasRects = Renderer::Primitives::getTileAtlasRects();
		bindUnit(kUnitTileAtlas, Renderer::Primitives::getTileAtlasTexture());
		glUniform1i(loc.tileAtlas, kUnitTileAtlas);
		glUniform1i(loc.tileAtlasRectCount, static_cast<GLint>(atlasRects.size()));
		if (!atlasRects.empty()) {
			glUniform4fv(loc.tileAtlasRects, static_cast<GLsizei>(atlasRects.size()), reinterpret_cast<const float*>(atlasRects.data()));
		}
		glUniform1i(loc.tileData, kUnitTileData);
		glUniform1i(loc.sdfTileMap, kUnitSdfTileMap);
		glUniform1i(loc.sdfNear, kUnitSdfNear);
		glUniform1i(loc.sdfFar, kUnitSdfFar);
		glUniform1i(loc.shoreProfile, kUnitShoreProfile);
		glUniform1i(loc.channelFrame, kUnitChannelFrame);

		quadVAO.bind();

		for (const Chunk* chunk : visibleChunks) {
			if (!chunk->isReady()) {
				continue;
			}

			const ChunkTextures& entry = ensureTextures(*chunk);
			bindUnit(kUnitTileData, entry.tileData.handle());
			bindWater(entry);

			WorldPosition origin = chunk->worldOrigin();
			ChunkCoordinate coord = chunk->coordinate();
			glUniform2f(loc.chunkOrigin, origin.x, origin.y);
			glUniform1f(loc.chunkWorldSize, kChunkWorldSize);
			glUniform2i(loc.chunkTileOrigin, coord.x * kChunkSize, coord.y * kChunkSize);

			glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
			lastChunks++;

			// Visible tile count for metrics: intersection of viewport and chunk, in tiles
			float visMinX = std::max(origin.x, visibleRect.x);
			float visMaxX = std::min(origin.x + kChunkWorldSize, visibleRect.x + visibleRect.width);
			float visMinY = std::max(origin.y, visibleRect.y);
			float visMaxY = std::min(origin.y + kChunkWorldSize, visibleRect.y + visibleRect.height);
			if (visMaxX > visMinX && visMaxY > visMinY) {
				lastTiles += static_cast<uint32_t>(std::ceil(visMaxX - visMinX) * std::ceil(visMaxY - visMinY));
			}
		}

		Renderer::GLVertexArray::unbind();
		glActiveTexture(GL_TEXTURE0);

		evictStaleTextures();

		// Restore GL state
		if (blendEnabled == GL_TRUE) {
			glEnable(GL_BLEND);
		} else {
			glDisable(GL_BLEND);
		}
		if (depthTestEnabled == GL_TRUE) {
			glEnable(GL_DEPTH_TEST);
		} else {
			glDisable(GL_DEPTH_TEST);
		}
		if (cullFaceEnabled == GL_TRUE) {
			glEnable(GL_CULL_FACE);
		} else {
			glDisable(GL_CULL_FACE);
		}
	}

} // namespace engine::world
