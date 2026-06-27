// Grass Scene - procedural groundcover, distributed by the asset's own placement controls.
//
// Each instance is a small hair-fine grass tuft (many variant meshes, so the field never
// reads as a repeated stamp). Their distribution is the SAME clumped algorithm the game's
// PlacementExecutor uses, driven by Groundcover_Grass's placement controls (spawnChance,
// clumpSize, clumpRadius): each 1m tile may seed a clump of tufts within a radius, so dense
// patches form with bare gaps between. Tuning Grass.xml here is exactly what ships.
//
// A gated cursor deform (reveal + bend) lives in instancing.glsl; it's wired but parked
// (cursor off-screen) while we dial in the resting look.

#include "SceneTypes.h"
#include <assets/AssetDefinition.h>
#include <assets/AssetRegistry.h>
#include <input/InputEvent.h>
#include <primitives/BatchRenderer.h>
#include <primitives/InstanceData.h>
#include <primitives/Primitives.h>
#include <scene/Scene.h>
#include <utils/Log.h>
#include <vector/Types.h>

#include <GL/glew.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

namespace {

	constexpr const char* kSceneName = "grass";

	// --- Palette: harmonized with the game's grass tile (#4a7040 base / Surface::Grass
	// 0.29,0.49,0.25). Muted greens so grass reads as a texture extending the ground, not
	// lime dots, at the zoomed-out game view. Tune freely. ---
	const Foundation::Color kGrassDark(0.21F, 0.33F, 0.16F, 1.0F); // shadow base (darker than ground)
	const Foundation::Color kGrassMid(0.31F, 0.47F, 0.24F, 1.0F);  // body ≈ ground green
	const Foundation::Color kGrassTip(0.46F, 0.63F, 0.33F, 1.0F);  // soft lit tip (lighter, still muted)
	const Foundation::Color kGround(0.29F, 0.44F, 0.25F, 1.0F);    // = game grass tile #4a7040

	// --- Tuning ---
	constexpr int	kVariantCount = 24;       // distinct tuft meshes (variety, no stamp)
	constexpr float kFieldWidthM = 200.0F;    // world meters (also tile count; kTileSize = 1m)
	constexpr float kFieldHeightM = 120.0F;
	constexpr float kPixelsPerMeter = 16.0F;
	constexpr float kCameraZoom = 8.0F;       // zoomed in so patches read (game is more top-down)
	constexpr float kGrassReach = 0.55F;      // max local vertex distance from a tuft base (m)
	constexpr float kGrassOpenness = 1.0F;    // resting tuft scale; 1.0 = full (reveal parked)
	constexpr float kCursorRadiusM = 4.0F;    // interaction radius (world m)
	constexpr float kCursorStrengthM = 0.6F;  // tip push at the cursor (world m)
	constexpr const char* kTuneBiome = "TemperateGrassland"; // which placement config to drive the field

	Foundation::Color lerpColor(const Foundation::Color& a, const Foundation::Color& b, float t) {
		return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
	}

	// One hair-fine, gently curved blade appended to the tuft mesh. Base near the tuft origin
	// (0,0); fans upward (-Y) along `angle`, tapering to a point.
	void buildBlade(
		renderer::TessellatedMesh& m,
		Foundation::Vec2		   base,
		float					   angle,
		float					   length,
		float					   curve,
		float					   halfWidth,
		const Foundation::Color&   colBase,
		const Foundation::Color&   colTip
	) {
		constexpr int	 kSeg = 4;
		Foundation::Vec2 dir(std::sin(angle), -std::cos(angle));	 // up = -Y
		Foundation::Vec2 perp(std::cos(angle), std::sin(angle)); // blade right
		auto			 start = static_cast<uint16_t>(m.vertices.size());

		for (int i = 0; i <= kSeg; ++i) {
			float			 t = static_cast<float>(i) / kSeg;
			Foundation::Vec2 p(
				base.x + dir.x * length * t + perp.x * curve * t * t,
				base.y + dir.y * length * t + perp.y * curve * t * t
			);
			float			  hw = halfWidth * (1.0F - t);
			Foundation::Color col = lerpColor(colBase, colTip, t);
			m.vertices.push_back({p.x + perp.x * hw, p.y + perp.y * hw});
			m.colors.push_back(col);
			m.vertices.push_back({p.x - perp.x * hw, p.y - perp.y * hw});
			m.colors.push_back(col);
		}
		for (int i = 0; i < kSeg; ++i) {
			auto a = static_cast<uint16_t>(start + i * 2);
			auto b = static_cast<uint16_t>(start + i * 2 + 1);
			auto c = static_cast<uint16_t>(start + (i + 1) * 2);
			auto d = static_cast<uint16_t>(start + (i + 1) * 2 + 1);
			m.indices.push_back(a); m.indices.push_back(b); m.indices.push_back(c);
			m.indices.push_back(b); m.indices.push_back(d); m.indices.push_back(c);
		}
	}

	renderer::TessellatedMesh buildTuftMesh(std::mt19937& rng) {
		renderer::TessellatedMesh m;
		std::uniform_int_distribution<int>	  nBladesD(8, 12);
		std::uniform_real_distribution<float> angleD(-0.5F, 0.5F);
		std::uniform_real_distribution<float> lenD(0.28F, 0.50F);
		std::uniform_real_distribution<float> curveD(-0.16F, 0.16F);
		std::uniform_real_distribution<float> hwD(0.005F, 0.009F); // hair-fine
		std::uniform_real_distribution<float> jitterD(-0.03F, 0.03F);
		std::uniform_real_distribution<float> tone(0.0F, 1.0F);

		int nBlades = nBladesD(rng);
		for (int i = 0; i < nBlades; ++i) {
			Foundation::Vec2  base(jitterD(rng), jitterD(rng) * 0.4F);
			Foundation::Color cBase = lerpColor(kGrassDark, kGrassMid, tone(rng));
			Foundation::Color cTip = lerpColor(kGrassMid, kGrassTip, 0.4F + 0.6F * tone(rng));
			buildBlade(m, base, angleD(rng), lenD(rng), curveD(rng), hwD(rng), cBase, cTip);
		}
		return m;
	}

	class GrassScene : public engine::IScene {
	  public:
		void onEnter() override {
			LOG_INFO(UI, "Grass Scene - placement-driven hair clumps");
			auto* br = Renderer::Primitives::getBatchRenderer();
			if (br == nullptr) {
				LOG_ERROR(UI, "No batch renderer available");
				return;
			}

			std::mt19937						   meshRng(1234);
			std::vector<renderer::TessellatedMesh> meshes;
			meshes.reserve(kVariantCount);
			m_variantTris.reserve(kVariantCount);
			for (int v = 0; v < kVariantCount; ++v) {
				renderer::TessellatedMesh mesh = buildTuftMesh(meshRng);
				m_variantTris.push_back(mesh.getTriangleCount());
				meshes.push_back(std::move(mesh));
			}

			// Distribution from the asset's own placement controls (same as the game).
			const auto* def = engine::assets::AssetRegistry::Get().getDefinition("Groundcover_Grass");
			const engine::assets::BiomePlacement* bp = def != nullptr ? def->placement.findBiome(kTuneBiome) : nullptr;
			if (bp == nullptr) {
				LOG_ERROR(UI, "Groundcover_Grass %s placement config not found", kTuneBiome);
				return;
			}
			generateInstances(*bp);

			m_handles.resize(kVariantCount);
			for (int v = 0; v < kVariantCount; ++v) {
				m_totalTufts += static_cast<uint32_t>(m_instancesByVariant[v].size());
				auto maxInst = static_cast<uint32_t>(std::max<size_t>(1, m_instancesByVariant[v].size()));
				m_handles[v] = br->uploadInstancedMesh(meshes[v], maxInst);
				if (!m_instancesByVariant[v].empty()) {
					m_drawCalls++;
					m_totalTris += m_variantTris[v] * m_instancesByVariant[v].size();
				}
			}
			LOG_INFO(UI, "Grass: %u tufts (placement clumped, spawnChance %.2f), %d variants, %zu tris, %u draws",
					 m_totalTufts, bp->spawnChance, kVariantCount, m_totalTris, m_drawCalls);
		}

		void update(float dt) override {
			float ms = dt * 1000.0F;
			m_frameMs = (m_frameMs <= 0.0F) ? ms : (m_frameMs * 0.9F + ms * 0.1F);
		}

		void render() override {
			glClearColor(kGround.r, kGround.g, kGround.b, 1.0F);
			glClear(GL_COLOR_BUFFER_BIT);

			auto* br = Renderer::Primitives::getBatchRenderer();
			if (br == nullptr) {
				return;
			}

			int viewW = 0;
			int viewH = 0;
			Renderer::Primitives::getViewport(viewW, viewH);
			br->setViewport(viewW, viewH);

			int logW = 0;
			int logH = 0;
			Renderer::Primitives::getLogicalViewport(logW, logH);

			const Foundation::Vec2 cameraPos(kFieldWidthM * 0.5F, kFieldHeightM * 0.5F);
			const float			   viewScale = kPixelsPerMeter * kCameraZoom;

			// Parked far off-screen until a real pointer move, so we evaluate resting grass.
			const Foundation::Vec2 cursorScreen = m_haveMouse ? m_mouse : Foundation::Vec2(-1.0e5F, -1.0e5F);
			const Foundation::Vec2 cursorWorld(
				(cursorScreen.x - static_cast<float>(logW) * 0.5F) / viewScale + cameraPos.x,
				(cursorScreen.y - static_cast<float>(logH) * 0.5F) / viewScale + cameraPos.y
			);

			GLuint prog = br->getShaderProgram();
			glUseProgram(prog);
			glUniform1f(glGetUniformLocation(prog, "u_bakedAlpha"), 1.0F);
			glUniform1i(glGetUniformLocation(prog, "u_grassMode"), 1);
			glUniform1f(glGetUniformLocation(prog, "u_grassOpenness"), kGrassOpenness);
			glUniform1f(glGetUniformLocation(prog, "u_grassReach"), kGrassReach);
			glUniform2f(glGetUniformLocation(prog, "u_cursorWorld"), cursorWorld.x, cursorWorld.y);
			glUniform1f(glGetUniformLocation(prog, "u_cursorRadius"), kCursorRadiusM);
			glUniform1f(glGetUniformLocation(prog, "u_cursorStrength"), kCursorStrengthM);

			for (int v = 0; v < kVariantCount; ++v) {
				auto& inst = m_instancesByVariant[v];
				if (inst.empty() || !m_handles[v].isValid()) {
					continue;
				}
				br->drawInstanced(m_handles[v], inst.data(), static_cast<uint32_t>(inst.size()), cameraPos, kCameraZoom, kPixelsPerMeter);
			}

			glUseProgram(prog);
			glUniform1i(glGetUniformLocation(prog, "u_grassMode"), 0);

			if (m_haveMouse) {
				Renderer::Primitives::drawCircle(
					{.center = {cursorScreen.x, cursorScreen.y},
					 .radius = kCursorRadiusM * viewScale,
					 .style = {.fill = Foundation::Color(0.0F, 0.0F, 0.0F, 0.0F),
							   .border = Foundation::BorderStyle{.color = Foundation::Color(1.0F, 1.0F, 1.0F, 0.5F), .width = 2.0F}}}
				);
			}

			const float fps = m_frameMs > 0.0F ? 1000.0F / m_frameMs : 0.0F;
			char		buf[224];
			std::snprintf(
				buf, sizeof(buf),
				"Groundcover_Grass tufts: %u   variants: %d   draw calls: %u   triangles: %zu   %.2f ms (%.0f fps)",
				m_totalTufts, kVariantCount, m_drawCalls, m_totalTris, m_frameMs, fps
			);
			Renderer::Primitives::drawText(
				{.text = buf, .position = {16.0F, 16.0F}, .scale = 1.0F, .color = Foundation::Color(1.0F, 1.0F, 1.0F, 1.0F)}
			);
		}

		void onExit() override {}

		std::string exportState() override {
			char buf[256];
			std::snprintf(buf, sizeof(buf), R"({"tufts": %u, "variants": %d, "drawCalls": %u, "triangles": %zu, "frameMs": %.3f})",
						  m_totalTufts, kVariantCount, m_drawCalls, m_totalTris, m_frameMs);
			return {buf};
		}

		const char* getName() const override { return kSceneName; }

		bool handleInput(UI::InputEvent& event) override {
			if (event.type == UI::InputEvent::Type::MouseMove) {
				m_mouse = event.position;
				m_haveMouse = true;
			}
			return false;
		}

	  private:
		// Replicates PlacementExecutor's Clumped distribution: per 1m tile, roll spawnChance;
		// on success seed a clump of clumpSize tufts scattered within clumpRadius. Dense patches,
		// bare gaps — all from the asset's controls.
		void generateInstances(const engine::assets::BiomePlacement& bp) {
			m_instancesByVariant.assign(kVariantCount, {});
			std::mt19937						  rng(99);
			std::uniform_real_distribution<float> chanceDist(0.0F, 1.0F);
			std::uniform_real_distribution<float> offsetDist(0.0F, 1.0F);
			std::uniform_int_distribution<int>	  varD(0, kVariantCount - 1);
			std::uniform_real_distribution<float> rotD(-0.30F, 0.30F);
			std::uniform_real_distribution<float> sclD(0.8F, 1.3F);
			std::uniform_real_distribution<float> brightD(-0.08F, 0.08F);
			std::uniform_int_distribution<int>	  sizeD(bp.clumping.clumpSizeMin, bp.clumping.clumpSizeMax);
			std::uniform_real_distribution<float> radD(bp.clumping.clumpRadiusMin, bp.clumping.clumpRadiusMax);

			const int tilesX = static_cast<int>(kFieldWidthM); // kTileSize = 1m
			const int tilesY = static_cast<int>(kFieldHeightM);
			for (int ty = 0; ty < tilesY; ++ty) {
				for (int tx = 0; tx < tilesX; ++tx) {
					if (chanceDist(rng) >= bp.spawnChance) {
						continue;
					}
					Foundation::Vec2 center(static_cast<float>(tx) + offsetDist(rng), static_cast<float>(ty) + offsetDist(rng));
					int				 clumpSize = sizeD(rng);
					float			 clumpRadius = radD(rng);
					std::uniform_real_distribution<float> offD(-clumpRadius, clumpRadius);
					for (int i = 0; i < clumpSize; ++i) {
						Foundation::Vec2 pos(center.x + offD(rng), center.y + offD(rng));
						float			 b = 0.9F + brightD(rng);
						int				 v = varD(rng);
						m_instancesByVariant[v].emplace_back(pos, rotD(rng), sclD(rng), Foundation::Color(b, b, b, 1.0F));
					}
				}
			}
		}

		std::vector<Renderer::InstancedMeshHandle>		 m_handles;
		std::vector<std::vector<Renderer::InstanceData>> m_instancesByVariant;
		std::vector<size_t>								 m_variantTris;
		size_t											 m_totalTris = 0;
		uint32_t										 m_totalTufts = 0;
		uint32_t										 m_drawCalls = 0;
		float											 m_frameMs = 0.0F;
		Foundation::Vec2								 m_mouse{0.0F, 0.0F};
		bool											 m_haveMouse = false;
	};

} // anonymous namespace

namespace ui_sandbox::scenes {
	extern const ui_sandbox::SceneInfo Grass = {kSceneName, []() { return std::make_unique<GrassScene>(); }};
} // namespace ui_sandbox::scenes
