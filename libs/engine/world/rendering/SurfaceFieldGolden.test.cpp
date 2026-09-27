// The land field in GLSL (shaders/includes/land.glsl) against its C++ evaluation
// (SurfaceField.cpp), terrain-polygons-architecture.md D16 step 6. The real include
// is compiled into a thin test shader whose main writes evaluateLand() for each
// pixel's world point (surface id, field, warp); the same points go through
// evaluateSurfaceField over the same render tiles, and the two must agree: the warp
// and the field within kTolerance, the surface exactly wherever no field is within
// kIdMargin of flipping it. The chunk sits 70 km out, where a float world coordinate
// would already have lost the warp's finest octave.
//
// Needs a GL 3.3 context; skips on a headless box, like the other GL tests.

#include "world/chunk/SurfaceField.h"

#include <random/HashNoise.h>
#include <shader/ShaderPreprocessor.h>

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

using namespace engine::world;

namespace {

	constexpr uint64_t		  kSeed = 0x5EEDF00DULL;
	const ChunkCoordinate kChunk{136, 2}; // tile origin (69632, 1024)
	constexpr int			  kPixels = 256;  // per side of each rendered region
	constexpr float			  kStepM  = 0.125F;

	/// Largest warp or field difference accepted between GLSL and C++ (fused
	/// multiply-adds on the GPU are the only expected source of any).
	constexpr float kTolerance = 1e-4F;
	/// Surfaces must agree wherever every field is at least this far from flipping one
	/// (ten times the largest field difference seen on a desktop GPU, ~1.4e-6).
	constexpr float kIdMargin = 1e-5F;

	int64_t floorDiv(int64_t a, int64_t b) {
		return a >= 0 ? a / b : -((-a + b - 1) / b);
	}

	// A world with every land surface, open water (for beds), blocks, lone tiles,
	// and 1-wide straight and diagonal paths, broken by plain 24 m squares where
	// tiles are interior.
	uint8_t syntheticSurface(int64_t tx, int64_t ty) {
		static constexpr std::array kLand = {
			Surface::Grass, Surface::Dirt, Surface::Sand, Surface::Rock, Surface::Snow,
			Surface::Mud, Surface::GrassTall, Surface::GrassShort, Surface::GrassMeadow,
		};
		const auto hash = [](int64_t x, int64_t y, uint32_t salt) {
			return foundation::hash3(static_cast<int32_t>(x), static_cast<int32_t>(y), 0, salt);
		};
		const uint32_t plain = hash(floorDiv(tx, 24), floorDiv(ty, 24), 5U);
		if (plain % 4U == 0U) {
			return static_cast<uint8_t>(kLand[(plain / 4U) % kLand.size()]);
		}
		if ((tx + ty) % 23 == 0) {
			return static_cast<uint8_t>(Surface::Dirt);
		}
		if (floorDiv(ty, 17) * 17 + 5 == ty) {
			return static_cast<uint8_t>(Surface::Sand);
		}
		if (hash(tx, ty, 7U) % 31U == 0U) {
			return static_cast<uint8_t>(kLand[hash(tx, ty, 8U) % kLand.size()]);
		}
		const uint32_t block = hash(floorDiv(tx, 6), floorDiv(ty, 5), 99U);
		if (block % 13U == 0U) {
			return static_cast<uint8_t>(Surface::Water);
		}
		return static_cast<uint8_t>(kLand[block % kLand.size()]);
	}

	/// The chunk's render tiles, built the way Chunk::computeRenderData builds them.
	std::vector<TileRenderData> syntheticRenderTiles() {
		constexpr int32_t kPaintMargin	 = kRenderApronTiles + kInteriorReachTiles;
		constexpr int32_t kSurfaceMargin = kPaintMargin + kBedReachTiles;
		constexpr int32_t kPaintSide	 = kChunkSize + 2 * kPaintMargin;
		constexpr int32_t kSurfaceSide	 = kChunkSize + 2 * kSurfaceMargin;
		const int64_t	  originX		 = static_cast<int64_t>(kChunk.x) * kChunkSize - kSurfaceMargin;
		const int64_t	  originY		 = static_cast<int64_t>(kChunk.y) * kChunkSize - kSurfaceMargin;
		std::vector<uint8_t> surfaces(static_cast<size_t>(kSurfaceSide) * kSurfaceSide);
		for (int32_t y = 0; y < kSurfaceSide; ++y) {
			for (int32_t x = 0; x < kSurfaceSide; ++x) {
				surfaces[static_cast<size_t>(y) * kSurfaceSide + static_cast<size_t>(x)] = syntheticSurface(originX + x, originY + y);
			}
		}
		return buildRenderTiles(paintSurfaces(surfaces, kPaintSide, kPaintSide), kRenderTilesSide, kRenderTilesSide);
	}

	std::filesystem::path landInclude() {
		const std::filesystem::path source = std::filesystem::path(__FILE__)
												 .parent_path() // libs/engine/world/rendering
												 .parent_path() // libs/engine/world
												 .parent_path() // libs/engine
												 .parent_path() // libs
											 / "renderer" / "shaders" / "includes" / "land.glsl";
		std::error_code ec;
		if (std::filesystem::exists(source, ec)) {
			return source;
		}
		return std::filesystem::current_path() / "shaders" / "includes" / "land.glsl";
	}

	constexpr const char* kVertexSource = R"(#version 330 core
void main() {
	vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
	gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

	constexpr const char* kFragmentMain = R"(
uniform vec2  u_regionOrigin; // chunk-local meters at the region's pixel (0, 0) corner
uniform float u_step;		  // meters per pixel
layout(location = 0) out vec4 outSample;
void main() {
	vec2	 local = u_regionOrigin + gl_FragCoord.xy * u_step;
	ivec2	 tile  = ivec2(floor(local));
	LandEval e	   = evaluateLand(tile, local - vec2(tile));
	outSample	   = vec4(float(e.painted), e.field, e.warp);
}
)";

	GLuint compile(GLenum type, const std::string& source, std::string& log) {
		const GLuint shader = glCreateShader(type);
		const char*	 text	= source.c_str();
		glShaderSource(shader, 1, &text, nullptr);
		glCompileShader(shader);
		GLint ok = 0;
		glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
		if (ok == 0) {
			GLint length = 0;
			glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
			log.resize(static_cast<size_t>(std::max(length, 1)));
			glGetShaderInfoLog(shader, length, nullptr, log.data());
			glDeleteShader(shader);
			return 0;
		}
		return shader;
	}

	// A hidden GL 3.3 core context for the test's lifetime; `ok` is false on a
	// headless box.
	struct GlContext {
		GLFWwindow* window = nullptr;
		bool		ok	   = false;

		GlContext() {
			if (glfwInit() != GLFW_TRUE) {
				return;
			}
			glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
			glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
			glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
			glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
			glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
			window = glfwCreateWindow(64, 64, "surface-field-golden", nullptr, nullptr);
			if (window == nullptr) {
				glfwTerminate();
				return;
			}
			glfwMakeContextCurrent(window);
			glewExperimental = GL_TRUE;
			if (glewInit() != GLEW_OK) {
				glfwDestroyWindow(window);
				glfwTerminate();
				window = nullptr;
				return;
			}
			ok = true;
		}

		~GlContext() {
			if (window != nullptr) {
				glfwDestroyWindow(window);
				glfwTerminate();
			}
		}

		GlContext(const GlContext&)			   = delete;
		GlContext& operator=(const GlContext&) = delete;
	};

	struct Region {
		float originX; // chunk-local meters
		float originY;
		const char* name;
	};

} // namespace

TEST(SurfaceFieldGoldenTest, GlslMatchesCpp) {
	GlContext gl;
	if (!gl.ok) {
		GTEST_SKIP() << "No GL 3.3 context (headless)";
	}

	const std::optional<std::string> land = Renderer::ShaderPreprocessor::process(landInclude());
	ASSERT_TRUE(land.has_value()) << "cannot read " << landInclude();
	std::string	 log;
	const GLuint vs = compile(GL_VERTEX_SHADER, kVertexSource, log);
	ASSERT_NE(vs, 0U) << log;
	const GLuint fs = compile(GL_FRAGMENT_SHADER, std::string("#version 330 core\n") + *land + kFragmentMain, log);
	ASSERT_NE(fs, 0U) << log;
	const GLuint program = glCreateProgram();
	glAttachShader(program, vs);
	glAttachShader(program, fs);
	glLinkProgram(program);
	GLint linked = 0;
	glGetProgramiv(program, GL_LINK_STATUS, &linked);
	ASSERT_NE(linked, 0);
	glDeleteShader(vs);
	glDeleteShader(fs);

	// Render tiles, uploaded as the game uploads them.
	const std::vector<TileRenderData> tiles = syntheticRenderTiles();
	GLuint							  tileTexture = 0;
	glGenTextures(1, &tileTexture);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, tileTexture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8UI, kRenderTilesSide, kRenderTilesSide, 0, GL_RG_INTEGER, GL_UNSIGNED_BYTE, tiles.data());
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	// The field's parameters, set the way ChunkRenderer::applyLandUniforms sets them.
	const SurfaceFieldParams params = defaultSurfaceFieldParams(kSeed);
	glUseProgram(program);
	auto at = [program](const char* name) { return glGetUniformLocation(program, name); };
	glUniform1i(at("u_tileData"), 0);
	const int64_t tileOriginX = static_cast<int64_t>(kChunk.x) * kChunkSize;
	const int64_t tileOriginY = static_cast<int64_t>(kChunk.y) * kChunkSize;
	glUniform2i(at("u_chunkTileOrigin"), static_cast<GLint>(tileOriginX), static_cast<GLint>(tileOriginY));
	glUniform1fv(at("u_landWarpFine"), static_cast<GLsizei>(kSurfaceCount), params.warpFineM.data());
	glUniform1fv(at("u_landWarpLow"), static_cast<GLsizei>(kSurfaceCount), params.warpLowM.data());
	glUniform2i(at("u_landWarpWavelength"), params.warpFineWavelengthM, params.warpLowWavelengthM);
	glUniform2f(
		at("u_landWarpInvWavelength"), 1.0F / static_cast<float>(params.warpFineWavelengthM), 1.0F / static_cast<float>(params.warpLowWavelengthM)
	);
	glUniform1f(at("u_landWarpFineGain"), params.warpFineGain);
	glUniform2ui(at("u_landWarpSeeds"), params.seeds.fine, params.seeds.low);
	glUniform1f(at("u_landThinFloor"), params.thinFloor);
	glUniform1f(at("u_landThinCeil"), params.thinCeil);
	glUniform1f(at("u_step"), kStepM);

	GLuint target = 0;
	glGenTextures(1, &target);
	glBindTexture(GL_TEXTURE_2D, target);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, kPixels, kPixels, 0, GL_RGBA, GL_FLOAT, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	GLuint framebuffer = 0;
	glGenFramebuffers(1, &framebuffer);
	glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target, 0);
	ASSERT_EQ(glCheckFramebufferStatus(GL_FRAMEBUFFER), static_cast<GLenum>(GL_FRAMEBUFFER_COMPLETE));
	GLuint vao = 0;
	glGenVertexArrays(1, &vao);
	glBindVertexArray(vao);
	glViewport(0, 0, kPixels, kPixels);
	glDisable(GL_BLEND);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, tileTexture);

	const RenderTileView view(tiles, kRenderTilesSide, kRenderTilesSide, tileOriginX - kRenderApronTiles, tileOriginY - kRenderApronTiles);

	// The middle of the chunk, and two corners, where the field reads the apron.
	const Region regions[] = {{200.0F, 312.0F, "middle"}, {0.0F, 0.0F, "origin corner"}, {480.0F, 480.0F, "far corner"}};
	std::vector<float> pixels(static_cast<size_t>(kPixels) * kPixels * 4);
	for (const Region& region : regions) {
		glUniform2f(at("u_regionOrigin"), region.originX, region.originY);
		glClear(GL_COLOR_BUFFER_BIT);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		glReadPixels(0, 0, kPixels, kPixels, GL_RGBA, GL_FLOAT, pixels.data());
		ASSERT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));

		int	  boundaries	  = 0;
		int	  undecided		  = 0;
		int	  undecidedDiffer = 0;
		int	  surfaceDiff	  = 0;
		int	  interior		  = 0;
		float worstWarp		  = 0.0F;
		float worstField	  = 0.0F;
		for (int j = 0; j < kPixels; ++j) {
			for (int i = 0; i < kPixels; ++i) {
				// gl_FragCoord is the pixel center, (i + 0.5, j + 0.5): exact in float at this step.
				const float	  localX = region.originX + (static_cast<float>(i) + 0.5F) * kStepM;
				const float	  localY = region.originY + (static_cast<float>(j) + 0.5F) * kStepM;
				const auto	  tileX	 = static_cast<int64_t>(std::floor(localX));
				const auto	  tileY	 = static_cast<int64_t>(std::floor(localY));
				const TilePoint p{tileOriginX + tileX, tileOriginY + tileY, localX - static_cast<float>(tileX), localY - static_cast<float>(tileY)};
				const SurfaceFieldSample cpu = evaluateSurfaceField(view, p, params);

				const float* gpu	  = &pixels[(static_cast<size_t>(j) * kPixels + static_cast<size_t>(i)) * 4];
				const auto	 gpuId	  = static_cast<int>(std::lround(gpu[0]));
				const float	 warpDiff = std::max(std::abs(gpu[2] - cpu.warpXM), std::abs(gpu[3] - cpu.warpYM));
				worstWarp			  = std::max(worstWarp, warpDiff);
				EXPECT_LE(warpDiff, kTolerance) << region.name << " pixel (" << i << ", " << j << ")";
				interior += cpu.warpXM == 0.0F && cpu.warpYM == 0.0F && cpu.field == 1.0F ? 1 : 0;

				if (cpu.margin < kIdMargin) {
					++undecided;
					undecidedDiffer += gpuId != static_cast<int>(cpu.surface) ? 1 : 0;
					continue;
				}
				boundaries += cpu.margin < 0.25F ? 1 : 0;
				if (gpuId != static_cast<int>(cpu.surface)) {
					++surfaceDiff;
					ADD_FAILURE() << region.name << " pixel (" << i << ", " << j << "): GLSL surface " << gpuId << ", C++ "
								  << static_cast<int>(cpu.surface) << " (margin " << cpu.margin << ")";
					continue;
				}
				const float fieldDiff = std::abs(gpu[1] - cpu.field);
				worstField			  = std::max(worstField, fieldDiff);
				EXPECT_LE(fieldDiff, kTolerance) << region.name << " pixel (" << i << ", " << j << ")";
			}
		}
		std::printf(
			"[golden] %s: worst warp diff %.3g m, worst field diff %.3g, %d near boundaries, %d interior, %d within the id margin "
			"(%d of them differ), %d surface mismatches\n",
			region.name,
			static_cast<double>(worstWarp),
			static_cast<double>(worstField),
			boundaries,
			interior,
			undecided,
			undecidedDiffer,
			surfaceDiff
		);
		EXPECT_GT(boundaries, kPixels * kPixels / 20) << region.name << ": the region should cross plenty of edges";
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glDeleteFramebuffers(1, &framebuffer);
	glDeleteVertexArrays(1, &vao);
	glDeleteTextures(1, &target);
	glDeleteTextures(1, &tileTexture);
	glDeleteProgram(program);
}
