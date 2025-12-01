// Tree Scene - Lua Asset Generator Demo
// Demonstrates Lua-based procedural tree generation.
// Shows 40 trees generated from the deciduous.lua script, each with unique seeds.

#include <assets/AssetBatcher.h>
#include <assets/AssetRegistry.h>
#include <assets/IAssetGenerator.h>
#include <graphics/Color.h>
#include <primitives/Primitives.h>
#include <scene/Scene.h>
#include <scene/SceneManager.h>
#include <utils/Log.h>
#include <vector/Tessellator.h>

#include <GL/glew.h>

namespace {

	// Tree configuration
	const char*	  kTreeAssetName = "Flora_TreeDeciduous";
	constexpr int kTreeCount = 40;
	constexpr int kGridCols = 8;
	constexpr int kGridRows = 5;

	class TreeScene : public engine::IScene {
	  public:
		void onEnter() override {
			LOG_INFO(UI, "Tree Scene - Lua Asset Generator Demo (40 trees)");

			// Load the tree definition from XML
			size_t loaded = engine::assets::AssetRegistry::Get().loadDefinitionsFromFolder("assets/definitions");
			LOG_INFO(UI, "Loaded %zu asset definitions", loaded);

			// Get the tree definition
			m_treeDef = engine::assets::AssetRegistry::Get().getDefinition(kTreeAssetName);
			if (m_treeDef == nullptr) {
				LOG_ERROR(UI, "Asset definition not found: %s", kTreeAssetName);
				return;
			}

			LOG_INFO(UI, "Found tree definition: %s", m_treeDef->defName.c_str());

			// Generate 40 trees with unique seeds
			renderer::Tessellator tessellator;
			for (int i = 0; i < kTreeCount; ++i) {
				uint32_t seed = 1000 + i * 777; // Unique seed for each tree

				engine::assets::GeneratedAsset asset;
				if (!engine::assets::AssetRegistry::Get().generateAsset(kTreeAssetName, seed, asset)) {
					LOG_WARNING(UI, "Failed to generate tree %d", i);
					continue;
				}

				TreeRenderData treeData;
				treeData.seed = seed;

				// Tessellate each path
				for (const auto& path : asset.paths) {
					if (path.vertices.size() < 3) {
						continue;
					}

					renderer::VectorPath vectorPath;
					vectorPath.vertices = path.vertices;
					vectorPath.isClosed = path.isClosed;

					renderer::TessellatedMesh pathMesh;
					if (!tessellator.Tessellate(vectorPath, pathMesh)) {
						continue;
					}

					PathRenderData renderData;
					renderData.mesh = std::move(pathMesh);
					renderData.color = path.fillColor;
					treeData.paths.push_back(std::move(renderData));
				}

				m_trees.push_back(std::move(treeData));
			}

			LOG_INFO(UI, "Generated %zu trees for rendering", m_trees.size());
		}

		void handleInput(float /*dt*/) override {}

		void update(float /*dt*/) override {}

		void render() override {
			// Clear background - grass/ground color
			glClearColor(0.35F, 0.5F, 0.25F, 1.0F);
			glClear(GL_COLOR_BUFFER_BIT);

			float windowWidth = Renderer::Primitives::PercentWidth(100.0F);
			float windowHeight = Renderer::Primitives::PercentHeight(100.0F);

			// Calculate grid cell size
			float cellWidth = windowWidth / static_cast<float>(kGridCols);
			float cellHeight = windowHeight / static_cast<float>(kGridRows);

			// Draw each tree in a grid
			for (size_t i = 0; i < m_trees.size(); ++i) {
				int col = static_cast<int>(i) % kGridCols;
				int row = static_cast<int>(i) / kGridCols;

				// Center of grid cell
				float treeX = cellWidth * (static_cast<float>(col) + 0.5F);
				float treeY = cellHeight * (static_cast<float>(row) + 0.5F);

				const auto& tree = m_trees[i];
				for (const auto& pathData : tree.paths) {
					if (pathData.mesh.vertices.empty()) {
						continue;
					}

					// Transform vertices to screen position
					std::vector<Foundation::Vec2> screenVerts;
					screenVerts.reserve(pathData.mesh.vertices.size());
					for (const auto& v : pathData.mesh.vertices) {
						screenVerts.push_back({treeX + v.x, treeY + v.y});
					}

					Renderer::Primitives::drawTriangles(
						{.vertices = screenVerts.data(),
						 .indices = pathData.mesh.indices.data(),
						 .vertexCount = screenVerts.size(),
						 .indexCount = pathData.mesh.indices.size(),
						 .color = pathData.color}
					);
				}
			}
		}

		void onExit() override {}

		std::string exportState() override {
			char buf[256];
			snprintf(buf, sizeof(buf), R"({"asset": "%s", "treeCount": %zu})", kTreeAssetName, m_trees.size());
			return {buf};
		}

		const char* getName() const override { return "tree"; }

	  private:
		struct PathRenderData {
			renderer::TessellatedMesh mesh;
			Foundation::Color		  color;
		};

		struct TreeRenderData {
			uint32_t					seed;
			std::vector<PathRenderData> paths;
		};

		const engine::assets::AssetDefinition* m_treeDef = nullptr;
		std::vector<TreeRenderData>			   m_trees;
	};

	// Register scene with SceneManager
	[[maybe_unused]] bool g_registered = []() {
		engine::SceneManager::Get().registerScene("tree", []() { return std::make_unique<TreeScene>(); });
		return true;
	}();

} // anonymous namespace
