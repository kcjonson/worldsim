#include "ChunkRenderer.h"

#include <debug/Tunables.h>
#include <primitives/Primitives.h>
#include <random/HashNoise.h>
#include <utils/Log.h>

#include <GL/glew.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <string>
#include <vector>

namespace worldview {

	namespace {
		// The tile-data texture is uploaded as raw TileRenderData, one RG8UI texel each.
		static_assert(sizeof(world::TileRenderData) == 2, "land.glsl reads TileRenderData as one RG8UI texel");
		static_assert(sizeof(world::HalfTexel) == 6, "RGB16F upload expects packed half texels");
		static_assert(sizeof(world::ByteTexel) == 4, "RGBA8 upload expects packed byte texels");

		using Field = world::TerrainDistanceField;

		// Near tiles per atlas row: the atlas is at most 32 * 66 = 2112 texels wide.
		constexpr int kNearAtlasMaxColumns = Field::kTilesPerSide;

		// Texture units: the macro 0, tile data 1, the distance field 2 to 6, then one
		// for each ground tile array.
		constexpr int kUnitGroundMacro	= 0;
		constexpr int kUnitTileData		= 1;
		constexpr int kUnitSdfTileMap	= 2;
		constexpr int kUnitSdfNear		= 3;
		constexpr int kUnitSdfFar		= 4;
		constexpr int kUnitShoreProfile = 5;
		constexpr int kUnitChannelFrame = 6;
		constexpr int kUnitGroundTiles	= 7;

		// The ground (docs/technical/ground-textures.md, "Shader"): mip levels a quarter
		// level blurrier than the screen's density, hex blending from 16 px/m up (one
		// sample at 12 looks the same and saves 0.05 to 0.10 ms), and Mikkelsen's gain r
		// of 0.75 at 48 px/m and up, 0.65 midway (in log2 px/m) and 0.5, none, at 16 and
		// under. Densities are screen pixels a meter.
		constexpr float				   kGroundLodBias			  = 0.25F;
		constexpr float				   kGroundHexMinPxPerMeter	  = 16.0F;
		constexpr std::array<float, 3> kGroundBlendGain			  = {0.75F, 0.65F, 0.5F};
		constexpr float				   kGroundBlendNearPxPerMeter = 48.0F;
		constexpr float				   kGroundBlendFarPxPerMeter  = 16.0F;
		constexpr float				   kGroundMacroAmp			  = 1.0F;
		constexpr uint32_t			   kSaltGroundCells			  = 0x6E0D7A11U;
		// The macro layer's value swing at its mid (10 m) and fine (5 m) octaves.
		constexpr float kGroundMacroMidAmp	= 0.045F;
		constexpr float kGroundMacroFineAmp = 0.035F;
		// land.glsl's hex lattice: 128 columns and 148 rows a chunk, each row skewed
		// half a column, so a chunk's first cell id is (128 cx - 74 cy, 148 cy).
		constexpr int32_t kGroundHexColumns = 128;
		constexpr int32_t kGroundHexRows	= 148;

		/// u_groundLayers.w: how land.glsl turns a hex cell's tile.
		int groundRotateCode(assets::GroundRotate rotate) {
			switch (rotate) {
				case assets::GroundRotate::Free:
					return 0;
				case assets::GroundRotate::Quarter:
					return 1;
				case assets::GroundRotate::None:
					return 2;
				case assets::GroundRotate::Mirror:
					return 3;
			}
			return 0;
		}

		uint32_t groundCellSeed(uint64_t worldSeed) {
			return foundation::hash3(static_cast<int32_t>(kSaltGroundCells), static_cast<int32_t>(worldSeed >> 32U), 0, static_cast<uint32_t>(worldSeed));
		}

		/// Mikkelsen's gain exponent log(1 - r) / log(0.5) at a screen density, r from
		/// the near, mid and far tunables, linear in log2 px/m.
		float groundBlendGainK(const float* gain, float screenPixelsPerMeter) {
			const float nearLog = std::log2(kGroundBlendNearPxPerMeter);
			const float farLog	= std::log2(kGroundBlendFarPxPerMeter);
			const float t		= std::clamp((std::log2(std::max(screenPixelsPerMeter, 1.0e-3F)) - farLog) / (nearLog - farLog), 0.0F, 1.0F);
			const float r		= t < 0.5F ? std::lerp(gain[2], gain[1], 2.0F * t) : std::lerp(gain[1], gain[0], 2.0F * t - 1.0F);
			return -std::log2(1.0F - std::clamp(r, 0.01F, 0.99F));
		}

		// Wraps shader time so its float precision holds; the animations jump once an hour.
		constexpr double kTimeWrapSeconds = 3600.0;

		/// A debug-server tunable (10.5) and the shader uniform it drives.
		struct ShaderTunable {
			const char*			 name;
			const char*			 uniform;
			std::array<float, 3> value;
			size_t				 count;
		};

		// Water: starting values from terrain-polygons-architecture.md 10.5; the ones
		// 10.5 leaves open (colors, patch thresholds, shimmer, foam motion, flow speed,
		// wetland mud gain) are WOR-460's first pass. Widths are meters.
		constexpr std::array kShaderTunables = {
			// Ground: Mikkelsen's weight exponent.
			ShaderTunable{"terrain/ground/blendContrast", "u_groundBlendContrast", {7.0F}, 1},

			ShaderTunable{"terrain/shore/wobbleAmp", "u_wobbleAmp", {0.12F}, 1},
			ShaderTunable{"terrain/shore/wobbleWavelength", "u_wobbleWavelength", {1.6F}, 1},
			ShaderTunable{"terrain/shore/alongAmp", "u_alongAmp", {0.35F}, 1},
			ShaderTunable{"terrain/shore/alongWavelength", "u_alongWavelength", {6.0F}, 1},
			ShaderTunable{"terrain/shore/drySandW", "u_drySandW", {1.6F}, 1},
			ShaderTunable{"terrain/shore/wetSandW", "u_wetSandW", {0.9F}, 1},
			ShaderTunable{"terrain/shore/mudW", "u_mudW", {1.1F}, 1},
			ShaderTunable{"terrain/shore/bandTail", "u_bandTail", {0.65F}, 1},
			ShaderTunable{"terrain/shore/wetlandMudGain", "u_wetlandMudGain", {1.8F}, 1},
			ShaderTunable{"terrain/shore/edgeW", "u_edgeW", {0.12F}, 1},
			ShaderTunable{"terrain/shore/edgeSlopeGain", "u_edgeSlopeGain", {2.5F}, 1},
			ShaderTunable{"terrain/shore/edgeMin", "u_edgeMin", {0.15F}, 1},
			ShaderTunable{"terrain/shore/edgeMax", "u_edgeMax", {0.85F}, 1},
			// 10.5 says 0.5, which leaves zoom 0.25 (exactly 0.5 m a pixel) on the fine
			// path; 10.2 means 2 px/m to take the far one, where patches only speckle.
			ShaderTunable{"terrain/shore/lodBandM", "u_lodBandM", {0.4F}, 1},

			ShaderTunable{"terrain/water/shallowsK", "u_shallowsK", {0.9F}, 1},
			ShaderTunable{"terrain/water/shallowsMin", "u_shallowsMin", {0.5F}, 1},
			ShaderTunable{"terrain/water/shallowsMax", "u_shallowsMax", {6.0F}, 1},
			ShaderTunable{"terrain/water/slopeMin", "u_slopeMin", {0.15F}, 1},
			ShaderTunable{"terrain/water/midAt", "u_midAt", {0.45F}, 1},
			ShaderTunable{"terrain/water/bedOpacity", "u_bedOpacity", {0.5F}, 1},
			ShaderTunable{"terrain/water/bedFade", "u_bedFade", {0.35F}, 1},
			ShaderTunable{"terrain/water/patchWavelength", "u_patchWavelength", {7.0F}, 1},
			// 10.5 says 0.45; at that amplitude open water read as camouflage, not a bottom.
			ShaderTunable{"terrain/water/patchAmp", "u_patchAmp", {0.2F}, 1},
			ShaderTunable{"terrain/water/barT0", "u_barT0", {0.3F}, 1},
			ShaderTunable{"terrain/water/barT1", "u_barT1", {0.6F}, 1},
			ShaderTunable{"terrain/water/weedT0", "u_weedT0", {0.3F}, 1},
			ShaderTunable{"terrain/water/weedT1", "u_weedT1", {0.6F}, 1},
			ShaderTunable{"terrain/water/shimmerAmp", "u_shimmerAmp", {0.025F}, 1},
			ShaderTunable{"terrain/water/shimmerFreq", "u_shimmerFreq", {0.9F}, 1},
			ShaderTunable{"terrain/water/shimmerDrift", "u_shimmerDrift", {0.25F}, 1},
			ShaderTunable{"terrain/water/foamW", "u_foamW", {0.4F}, 1},
			ShaderTunable{"terrain/water/foamWavelength", "u_foamWavelength", {1.2F}, 1},
			ShaderTunable{"terrain/water/foamDrift", "u_foamDrift", {0.3F}, 1},

			ShaderTunable{"terrain/river/thalwegInner", "u_thalwegInner", {0.6F}, 1},
			ShaderTunable{"terrain/river/thalwegOuter", "u_thalwegOuter", {1.1F}, 1},
			ShaderTunable{"terrain/river/thalwegDepth", "u_thalwegDepth", {0.9F}, 1},
			// 10.5 gives the riffle frequency as 2 pi / 1.5 m; the shader snaps the wavelength to tile the 64 m arc wrap.
			ShaderTunable{"terrain/river/riffleWavelength", "u_riffleWavelength", {1.5F}, 1},
			ShaderTunable{"terrain/river/riffleAmp", "u_riffleAmp", {0.3F}, 1},
			ShaderTunable{"terrain/river/poolAmp", "u_poolAmp", {0.35F}, 1},
			ShaderTunable{"terrain/river/flowSpeed", "u_flowSpeed", {0.6F}, 1},

			ShaderTunable{"terrain/color/drySand", "u_drySand", {0.84F, 0.75F, 0.53F}, 3},
			ShaderTunable{"terrain/color/wetSand", "u_wetSand", {0.62F, 0.53F, 0.37F}, 3},
			ShaderTunable{"terrain/color/mudBank", "u_mudBank", {0.36F, 0.28F, 0.18F}, 3},
			ShaderTunable{"terrain/color/bedGrass", "u_bedGrass", {0.30F, 0.38F, 0.24F}, 3},
			ShaderTunable{"terrain/color/edge", "u_edgeColor", {0.16F, 0.13F, 0.09F}, 3},
			ShaderTunable{"terrain/color/waterShallow", "u_waterShallow", {0.42F, 0.64F, 0.64F}, 3},
			ShaderTunable{"terrain/color/waterMid", "u_waterMid", {0.16F, 0.44F, 0.55F}, 3},
			ShaderTunable{"terrain/color/waterDeep", "u_waterDeep", {0.07F, 0.24F, 0.42F}, 3},
			ShaderTunable{"terrain/color/riffleLight", "u_riffleLight", {0.70F, 0.84F, 0.86F}, 3},
			ShaderTunable{"terrain/color/bar", "u_barColor", {0.62F, 0.66F, 0.52F}, 3},
			ShaderTunable{"terrain/color/weed", "u_weedColor", {0.20F, 0.36F, 0.26F}, 3},
			ShaderTunable{"terrain/color/foam", "u_foam", {0.92F, 0.95F, 0.95F}, 3},
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

	ChunkRenderer::ChunkRenderer(float pixelsPerMeter, const GroundTileBank& tiles)
		: groundTiles(&tiles),
		  pixelsPerMeterValue(pixelsPerMeter) {}

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
		for (size_t i = 0; i < loc.groundTiles.size(); ++i) {
			loc.groundTiles[i] = glGetUniformLocation(program, ("u_groundTiles" + std::to_string(i)).c_str());
		}
		loc.groundLayers = glGetUniformLocation(program, "u_groundLayers");
		loc.groundParams = glGetUniformLocation(program, "u_groundParams");
		loc.groundTintBase = glGetUniformLocation(program, "u_groundTintBase");
		loc.groundTintLush = glGetUniformLocation(program, "u_groundTintLush");
		loc.groundSeed = glGetUniformLocation(program, "u_groundSeed");
		loc.groundCellOrigin = glGetUniformLocation(program, "u_groundCellOrigin");
		loc.groundMacroValue = glGetUniformLocation(program, "u_groundMacroValue");
		loc.groundHex = glGetUniformLocation(program, "u_groundHex");
		loc.groundBlendGainK = glGetUniformLocation(program, "u_groundBlendGainK");
		loc.groundMacro = glGetUniformLocation(program, "u_groundMacro");
		loc.hasWater = glGetUniformLocation(program, "u_hasWater");
		loc.sdfTileMap = glGetUniformLocation(program, "u_sdfTileMap");
		loc.sdfNear = glGetUniformLocation(program, "u_sdfNear");
		loc.sdfNearColumns = glGetUniformLocation(program, "u_sdfNearColumns");
		loc.sdfFar = glGetUniformLocation(program, "u_sdfFar");
		loc.shoreProfile = glGetUniformLocation(program, "u_shoreProfile");
		loc.channelFrame = glGetUniformLocation(program, "u_channelFrame");
		loc.time = glGetUniformLocation(program, "u_time");
		loc.metersPerPixel = glGetUniformLocation(program, "u_metersPerPixel");
		loc.landWarpFine = glGetUniformLocation(program, "u_landWarpFine");
		loc.landWarpLow = glGetUniformLocation(program, "u_landWarpLow");
		loc.landWarpWavelength = glGetUniformLocation(program, "u_landWarpWavelength");
		loc.landWarpInvWavelength = glGetUniformLocation(program, "u_landWarpInvWavelength");
		loc.landWarpFineGain = glGetUniformLocation(program, "u_landWarpFineGain");
		loc.landWarpSeeds = glGetUniformLocation(program, "u_landWarpSeeds");
		loc.landThinFloor = glGetUniformLocation(program, "u_landThinFloor");
		loc.landThinCeil = glGetUniformLocation(program, "u_landThinCeil");
		loc.landWarpFineOctaves = glGetUniformLocation(program, "u_landWarpFineOctaves");
		loc.landPair = glGetUniformLocation(program, "u_landPair");

		Foundation::Tunables& tunables = Foundation::Tunables::instance();
		world::SurfaceFieldTunables::get();
		auto addLook = [&tunables](const std::string& name, const float& value) {
			return tunables.add(name, std::span<const float>(&value, 1));
		};
		groundLook.lodBias = addLook("terrain/ground/lodBias", kGroundLodBias);
		groundLook.hexMinPxPerMeter = addLook("terrain/ground/hexMinPxPerMeter", kGroundHexMinPxPerMeter);
		groundLook.blendGain = tunables.add("terrain/ground/blendGain", std::span<const float>(kGroundBlendGain));
		groundLook.macroAmp = addLook("terrain/ground/macroAmp", kGroundMacroAmp);
		groundLook.macroMidAmp = addLook("terrain/ground/macroMidAmp", kGroundMacroMidAmp);
		groundLook.macroFineAmp = addLook("terrain/ground/macroFineAmp", kGroundMacroFineAmp);
		tunableUniforms.clear();
		tunableUniforms.reserve(kShaderTunables.size());
		for (const ShaderTunable& t : kShaderTunables) {
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
		const world::HalfTexel landTexel  = Field::kLandSdfTexel;
		const world::HalfTexel zeroHalf{};
		const world::ByteTexel zeroByte{};
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

	size_t ChunkRenderer::uploadDistanceField(ChunkTextures& entry, const world::TerrainDistanceField& field) {
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
			std::vector<world::HalfTexel> atlas(static_cast<size_t>(width) * static_cast<size_t>(height), Field::kLandSdfTexel);
			for (size_t tile = 0; tile < nearTiles; ++tile) {
				const int cellX = static_cast<int>(tile % static_cast<size_t>(columns)) * stride;
				const int cellY = static_cast<int>(tile / static_cast<size_t>(columns)) * stride;
				const world::HalfTexel* src = field.nearTexels.data() + tile * Field::kNearTileTexelCount;
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
			bytes += atlas.size() * sizeof(world::HalfTexel) + field.tileMap.size() * sizeof(uint16_t);
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

	ChunkRenderer::ChunkTextures& ChunkRenderer::ensureTextures(const world::Chunk& chunk) {
		ChunkTextures& entry = textureCache[chunk.coordinate()];
		entry.lastAccessFrame = frameCounter;

		const uint32_t tileVersion = chunk.renderDataVersion();
		// Render tile rows are 2 * 518 = 1036 bytes and macro rows 4 * 257 = 1028, so the
		// default 4-byte unpack alignment holds for both.
		if (!entry.tileData.isValid()) {
			glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + kUnitTileData));
			entry.tileData =
				makeTexture(world::kRenderTilesSide, world::kRenderTilesSide, GL_RG8UI, GL_RG_INTEGER, GL_UNSIGNED_BYTE, chunk.renderData(), GL_NEAREST);
			glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + kUnitGroundMacro));
			entry.groundMacro =
				makeTexture(world::kGroundMacroSide, world::kGroundMacroSide, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, chunk.groundMacro(), GL_LINEAR);
			entry.tileVersion = tileVersion;
		} else if (entry.tileVersion != tileVersion && takeStaleBudget(kChunkTileBytes)) {
			glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + kUnitTileData));
			entry.tileData.bind();
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, world::kRenderTilesSide, world::kRenderTilesSide, GL_RG_INTEGER, GL_UNSIGNED_BYTE, chunk.renderData());
			glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + kUnitGroundMacro));
			entry.groundMacro.bind();
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, world::kGroundMacroSide, world::kGroundMacroSide, GL_RGBA, GL_UNSIGNED_BYTE, chunk.groundMacro());
			entry.tileVersion = tileVersion;
		}

		const world::TerrainDistanceField& field = chunk.terrainDistanceField();
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
			total += entry.tileData.isValid() ? kChunkTileBytes : 0;
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

		std::vector<std::pair<world::ChunkCoordinate, uint64_t>> byAge;
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

	void ChunkRenderer::applyLandUniforms(uint64_t worldSeed, float metersPerPixel) const {
		// The reciprocals are fractalNoise2SplitPair's own 1.0F / wavelength, so the
		// shader scales by the same floats the CPU evaluation does.
		auto inverse = [](int32_t wavelength) { return 1.0F / static_cast<float>(wavelength); };

		const world::SurfaceFieldParams field = world::SurfaceFieldTunables::get().params(worldSeed);
		glUniform1fv(loc.landWarpFine, static_cast<GLsizei>(world::kSurfaceCount), field.warpFineM.data());
		glUniform1fv(loc.landWarpLow, static_cast<GLsizei>(world::kSurfaceCount), field.warpLowM.data());
		glUniform2i(loc.landWarpWavelength, field.warpFineWavelengthM, field.warpLowWavelengthM);
		glUniform2f(loc.landWarpInvWavelength, inverse(field.warpFineWavelengthM), inverse(field.warpLowWavelengthM));
		glUniform1f(loc.landWarpFineGain, field.warpFineGain);
		glUniform2ui(loc.landWarpSeeds, field.seeds.fine, field.seeds.low);
		glUniform1f(loc.landThinFloor, field.thinFloor);
		glUniform1f(loc.landThinCeil, field.thinCeil);
		glUniform1i(loc.landWarpFineOctaves, world::landWarpFineOctaves(metersPerPixel, field.warpFineWavelengthM));
	}

	void ChunkRenderer::applyGroundUniforms(uint64_t worldSeed, float screenPixelsPerMeter) const {
		const std::span<const GroundTileBank::Array> arrays = groundTiles->arrays();
		for (size_t i = 0; i < loc.groundTiles.size(); ++i) {
			const int unit = kUnitGroundTiles + static_cast<int>(i);
			glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + unit));
			glBindTexture(GL_TEXTURE_2D_ARRAY, i < arrays.size() ? arrays[i].texture : 0);
			glUniform1i(loc.groundTiles[i], unit);
		}
		glUniform1i(loc.groundMacro, kUnitGroundMacro);

		// Folded here so the shader doesn't per pixel: the variant pick's constants
		// (land.glsl groundVariant), the macro tint mix(1, mix(dry, lush, R), amp) as
		// base + lush * R, and the pair table's mip window and pixel floor (GroundPairs.h).
		constexpr size_t			  kCount = world::kSurfaceCount;
		std::array<GLint, kCount * 4> layers{};
		std::array<float, kCount * 4> params{};
		std::array<float, kCount * 3> tintBase{};
		std::array<float, kCount * 3> tintLush{};
		std::array<float, kCount>	  mipLevels{};
		const float					  bias = *groundLook.lodBias;
		const float					  macroAmp = *groundLook.macroAmp;
		for (size_t id = 0; id < kCount; ++id) {
			const GroundTileBank::Surface& s = groundTiles->surface(static_cast<world::Surface>(id));
			const int					   variants = std::max(s.layerCount, 1);
			const float					   amp = s.macro.amplitude * macroAmp;
			layers[id * 4] = s.array;
			layers[id * 4 + 1] = s.firstLayer;
			layers[id * 4 + 2] = variants;
			layers[id * 4 + 3] = groundRotateCode(s.rotate) | (s.hashSurface << 2);
			mipLevels[id] = s.texelsPerMeter > 0.0F ? std::log2(s.texelsPerMeter / screenPixelsPerMeter) + bias : 0.0F;
			params[id * 4] = s.invPeriod;
			params[id * 4 + 1] = mipLevels[id];
			params[id * 4 + 2] = 1.0F / static_cast<float>(variants);
			params[id * 4 + 3] = 0.5F / static_cast<float>(variants) - 0.5F;
			for (size_t c = 0; c < 3; ++c) {
				tintBase[id * 3 + c] = 1.0F + amp * (s.macro.dry[c] - 1.0F);
				tintLush[id * 3 + c] = amp * (s.macro.lush[c] - s.macro.dry[c]);
			}
		}
		glUniform4iv(loc.groundLayers, static_cast<GLsizei>(kCount), layers.data());
		glUniform4fv(loc.groundParams, static_cast<GLsizei>(kCount), params.data());
		glUniform3fv(loc.groundTintBase, static_cast<GLsizei>(kCount), tintBase.data());
		glUniform3fv(loc.groundTintLush, static_cast<GLsizei>(kCount), tintLush.data());
		std::array<float, kLandPairFloats> pairs{};
		packGroundPairs(groundTiles->pairs(), mipLevels, 1.0F / screenPixelsPerMeter, pairs);
		glUniform4fv(loc.landPair, static_cast<GLsizei>(kLandPairs * kLandPairVec4s), pairs.data());
		const float mid = *groundLook.macroMidAmp;
		const float fine = *groundLook.macroFineAmp;
		glUniform3f(loc.groundMacroValue, 1.0F - mid - fine, 2.0F * mid, 2.0F * fine);
		glUniform1ui(loc.groundSeed, groundCellSeed(worldSeed));
		glUniform1i(loc.groundHex, screenPixelsPerMeter >= *groundLook.hexMinPxPerMeter ? 1 : 0);
		glUniform1f(loc.groundBlendGainK, groundBlendGainK(groundLook.blendGain, screenPixelsPerMeter));
	}

	void ChunkRenderer::render(const world::ChunkManager& chunkManager, const world::WorldCamera& camera, int viewportWidth, int viewportHeight) {
		lastTiles = 0;
		lastChunks = 0;
		staleBytesThisFrame = 0;
		frameCounter++;

		Foundation::Rect visibleRect = camera.getVisibleRect(viewportWidth, viewportHeight, pixelsPerMeterValue);
		auto [minCorner, maxCorner] = camera.getVisibleCorners(viewportWidth, viewportHeight, pixelsPerMeterValue);
		std::vector<const world::Chunk*> visibleChunks = chunkManager.getVisibleChunks(minCorner, maxCorner);
		if (visibleChunks.empty()) {
			return;
		}

		if (!initGL()) {
			return;
		}
		if (!groundTiles->ready()) {
			if (!loggedUnreadyGround) {
				loggedUnreadyGround = true;
				LOG_ERROR(Renderer, "ChunkRenderer: the ground tiles aren't baked; nothing to paint the ground with");
			}
			return;
		}

		// Flush any pending batched geometry so draw order stays correct
		Renderer::Primitives::flush();

		// Save GL state before modifying
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
		const float metersPerPixel = 1.0F / (pixelsPerMeterValue * camera.zoom());
		glUniform1f(loc.metersPerPixel, metersPerPixel);
		const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime).count();
		glUniform1f(loc.time, static_cast<float>(std::fmod(seconds, kTimeWrapSeconds)));
		applyTunables();
		applyLandUniforms(visibleChunks.front()->worldSeed(), metersPerPixel);
		// Mip levels and the hex cutoff go by the framebuffer's pixels, which a HiDPI
		// display has more of than the logical viewport the camera works in.
		int framebufferWidth = 0;
		int framebufferHeight = 0;
		Renderer::Primitives::getViewport(framebufferWidth, framebufferHeight);
		const float pixelScale =
			viewportWidth > 0 && framebufferWidth > 0 ? std::clamp(static_cast<float>(framebufferWidth) / static_cast<float>(viewportWidth), 1.0F, 4.0F) : 1.0F;
		applyGroundUniforms(visibleChunks.front()->worldSeed(), pixelScale / metersPerPixel);
		glUniform1i(loc.tileData, kUnitTileData);
		glUniform1i(loc.sdfTileMap, kUnitSdfTileMap);
		glUniform1i(loc.sdfNear, kUnitSdfNear);
		glUniform1i(loc.sdfFar, kUnitSdfFar);
		glUniform1i(loc.shoreProfile, kUnitShoreProfile);
		glUniform1i(loc.channelFrame, kUnitChannelFrame);

		quadVAO.bind();

		for (const world::Chunk* chunk : visibleChunks) {
			if (!chunk->isReady()) {
				continue;
			}

			const ChunkTextures& entry = ensureTextures(*chunk);
			bindUnit(kUnitTileData, entry.tileData.handle());
			bindUnit(kUnitGroundMacro, entry.groundMacro.handle());
			bindWater(entry);

			world::WorldPosition origin = chunk->worldOrigin();
			world::ChunkCoordinate coord = chunk->coordinate();
			glUniform2f(loc.chunkOrigin, origin.x, origin.y);
			glUniform1f(loc.chunkWorldSize, world::kChunkWorldSize);
			glUniform2i(loc.chunkTileOrigin, coord.x * world::kChunkSize, coord.y * world::kChunkSize);
			glUniform2i(loc.groundCellOrigin, kGroundHexColumns * coord.x - kGroundHexRows / 2 * coord.y, kGroundHexRows * coord.y);

			glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
			lastChunks++;

			// Visible tile count for metrics: intersection of viewport and chunk, in tiles
			float visMinX = std::max(origin.x, visibleRect.x);
			float visMaxX = std::min(origin.x + world::kChunkWorldSize, visibleRect.x + visibleRect.width);
			float visMinY = std::max(origin.y, visibleRect.y);
			float visMaxY = std::min(origin.y + world::kChunkWorldSize, visibleRect.y + visibleRect.height);
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

} // namespace worldview
