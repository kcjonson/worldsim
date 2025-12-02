// SceneTypes.cpp - Scene registry initialization for ui-sandbox

#include "SceneTypes.h"

// Each scene exports a SceneInfo - forward declare them here
namespace ui_sandbox::scenes {
	using ui_sandbox::SceneInfo;
	extern const SceneInfo Shapes;
	extern const SceneInfo Arena;
	extern const SceneInfo Handle;
	extern const SceneInfo Button;
	extern const SceneInfo TextInput;
	extern const SceneInfo Grass;
	extern const SceneInfo VectorPerf;
	extern const SceneInfo VectorStar;
	extern const SceneInfo Svg;
	extern const SceneInfo Clip;
	extern const SceneInfo Layer;
	extern const SceneInfo TextShapes;
	extern const SceneInfo SdfMinimal;
	extern const SceneInfo InputTest;
	extern const SceneInfo Tree;
} // namespace ui_sandbox::scenes

namespace ui_sandbox {

	void initializeSceneManager() {
		// Single list: enum -> scene info (each scene mentioned exactly once)
		const std::pair<SceneType, const SceneInfo*> allScenes[] = {
			{SceneType::Shapes, &scenes::Shapes},
			{SceneType::Arena, &scenes::Arena},
			{SceneType::Handle, &scenes::Handle},
			{SceneType::Button, &scenes::Button},
			{SceneType::TextInput, &scenes::TextInput},
			{SceneType::Grass, &scenes::Grass},
			{SceneType::VectorPerf, &scenes::VectorPerf},
			{SceneType::VectorStar, &scenes::VectorStar},
			{SceneType::Svg, &scenes::Svg},
			{SceneType::Clip, &scenes::Clip},
			{SceneType::Layer, &scenes::Layer},
			{SceneType::TextShapes, &scenes::TextShapes},
			{SceneType::SdfMinimal, &scenes::SdfMinimal},
			{SceneType::InputTest, &scenes::InputTest},
			{SceneType::Tree, &scenes::Tree},
		};

		// Build registry and names from the single list
		engine::SceneRegistry							  registry;
		std::unordered_map<engine::SceneKey, std::string> names;

		for (const auto& [type, info] : allScenes) {
			registry[toKey(type)] = info->factory;
			names[toKey(type)] = info->name;
		}

		engine::SceneManager::Get().initialize(std::move(registry), std::move(names));
	}

} // namespace ui_sandbox
