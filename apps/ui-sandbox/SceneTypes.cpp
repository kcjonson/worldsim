// SceneTypes.cpp - Scene registry initialization for ui-sandbox
// Each scene exports its factory function and name; this file collects them

#include "SceneTypes.h"
#include <scene/Scene.h>
#include <memory>

// Forward declarations of scene factory functions (defined in each scene .cpp)
namespace ui_sandbox::scenes {
	std::unique_ptr<engine::IScene> createShapesScene();
	const char* getShapesSceneName();

	std::unique_ptr<engine::IScene> createArenaScene();
	const char* getArenaSceneName();

	std::unique_ptr<engine::IScene> createHandleScene();
	const char* getHandleSceneName();

	std::unique_ptr<engine::IScene> createButtonScene();
	const char* getButtonSceneName();

	std::unique_ptr<engine::IScene> createTextInputScene();
	const char* getTextInputSceneName();

	std::unique_ptr<engine::IScene> createGrassScene();
	const char* getGrassSceneName();

	std::unique_ptr<engine::IScene> createVectorPerfScene();
	const char* getVectorPerfSceneName();

	std::unique_ptr<engine::IScene> createVectorStarScene();
	const char* getVectorStarSceneName();

	std::unique_ptr<engine::IScene> createSvgScene();
	const char* getSvgSceneName();

	std::unique_ptr<engine::IScene> createClipScene();
	const char* getClipSceneName();

	std::unique_ptr<engine::IScene> createLayerScene();
	const char* getLayerSceneName();

	std::unique_ptr<engine::IScene> createTextShapesScene();
	const char* getTextShapesSceneName();

	std::unique_ptr<engine::IScene> createSdfMinimalScene();
	const char* getSdfMinimalSceneName();

	std::unique_ptr<engine::IScene> createInputTestScene();
	const char* getInputTestSceneName();

	std::unique_ptr<engine::IScene> createTreeScene();
	const char* getTreeSceneName();
} // namespace ui_sandbox::scenes

namespace ui_sandbox {

void initializeSceneManager() {
	using namespace scenes;

	// Build registry: enum -> factory
	engine::SceneRegistry registry;
	registry[toKey(SceneType::Shapes)] = createShapesScene;
	registry[toKey(SceneType::Arena)] = createArenaScene;
	registry[toKey(SceneType::Handle)] = createHandleScene;
	registry[toKey(SceneType::Button)] = createButtonScene;
	registry[toKey(SceneType::TextInput)] = createTextInputScene;
	registry[toKey(SceneType::Grass)] = createGrassScene;
	registry[toKey(SceneType::VectorPerf)] = createVectorPerfScene;
	registry[toKey(SceneType::VectorStar)] = createVectorStarScene;
	registry[toKey(SceneType::Svg)] = createSvgScene;
	registry[toKey(SceneType::Clip)] = createClipScene;
	registry[toKey(SceneType::Layer)] = createLayerScene;
	registry[toKey(SceneType::TextShapes)] = createTextShapesScene;
	registry[toKey(SceneType::SdfMinimal)] = createSdfMinimalScene;
	registry[toKey(SceneType::InputTest)] = createInputTestScene;
	registry[toKey(SceneType::Tree)] = createTreeScene;

	// Build names: enum -> name (each scene declares its own name)
	std::unordered_map<engine::SceneKey, std::string> names;
	names[toKey(SceneType::Shapes)] = getShapesSceneName();
	names[toKey(SceneType::Arena)] = getArenaSceneName();
	names[toKey(SceneType::Handle)] = getHandleSceneName();
	names[toKey(SceneType::Button)] = getButtonSceneName();
	names[toKey(SceneType::TextInput)] = getTextInputSceneName();
	names[toKey(SceneType::Grass)] = getGrassSceneName();
	names[toKey(SceneType::VectorPerf)] = getVectorPerfSceneName();
	names[toKey(SceneType::VectorStar)] = getVectorStarSceneName();
	names[toKey(SceneType::Svg)] = getSvgSceneName();
	names[toKey(SceneType::Clip)] = getClipSceneName();
	names[toKey(SceneType::Layer)] = getLayerSceneName();
	names[toKey(SceneType::TextShapes)] = getTextShapesSceneName();
	names[toKey(SceneType::SdfMinimal)] = getSdfMinimalSceneName();
	names[toKey(SceneType::InputTest)] = getInputTestSceneName();
	names[toKey(SceneType::Tree)] = getTreeSceneName();

	// Initialize SceneManager with our registry
	engine::SceneManager::Get().initialize(std::move(registry), std::move(names));
}

} // namespace ui_sandbox
