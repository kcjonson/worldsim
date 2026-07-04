#include "DecorativePlanet.h"

#include "GameStartConfig.h"

#include <worldgen/io/PlanetIO.h>

#include <primitives/Primitives.h>
#include <theme/Tokens.h>
#include <utils/Log.h>
#include <utils/ResourcePath.h>

#include <algorithm>
#include <cstdint>
#include <future>
#include <mutex>
#include <utility>

namespace world_sim {

namespace {

	// Orbit distance (planet radii) that keeps the whole disc inside the render
	// rect with a margin (disc fills ~82% of the rect height at 45 deg FOV).
	constexpr float kViewDistance = 3.1F;
	constexpr float kFadeSeconds = 0.6F;
	// Prototype: planet opacity 0.95, scrim linear-gradient(bg_void 26%, transparent 68%).
	constexpr float kDiscOpacity = 0.95F;
	constexpr float kScrimAlpha = 0.9F;
	constexpr float kScrimSolidFrac = 0.26F;
	constexpr float kScrimFadeFrac = 0.68F;

	// Session planet cache. The shipped planet file is large, so it is loaded
	// off-thread at most once per run and shared by every scene's backdrop. A
	// missing or unloadable file leaves the cache empty for the whole session.
	std::mutex planetMutex;
	std::shared_ptr<const worldgen::GeneratedWorld> cachedPlanet;
	bool loadStarted = false;
	bool loadDone = false;
	std::future<void> loadTask; // static so process exit joins the loader

	void startSessionLoad() {
		loadTask = std::async(std::launch::async, []() {
			std::shared_ptr<const worldgen::GeneratedWorld> planet;
			if (auto path = Foundation::findResource(kQuickstartPlanetResource)) {
				planet = worldgen::loadPlanet(*path);
				if (planet) {
					LOG_INFO(Game, "DecorativePlanet: quickstart planet loaded (n=%u)",
					         planet->params.gridSubdivision);
				}
			} else {
				LOG_INFO(Game, "DecorativePlanet: no quickstart planet; backdrop disabled");
			}
			std::lock_guard<std::mutex> lock(planetMutex);
			cachedPlanet = std::move(planet);
			loadDone = true;
		});
	}

} // namespace

void DecorativePlanet::update(float dt) {
	if (!worldSet) {
		std::lock_guard<std::mutex> lock(planetMutex);
		if (!loadStarted) {
			loadStarted = true;
			startSessionLoad();
		}
		if (loadDone && cachedPlanet) {
			globe.setWorld(cachedPlanet);
			globe.setViewDistance(kViewDistance);
			worldSet = true;
		}
	}
	if (!worldSet) return;

	globe.update(dt);
	if (globe.hasContent()) {
		fade = std::min(1.0F, fade + dt / kFadeSeconds);
	}
}

void DecorativePlanet::render(const Foundation::Rect& rect, float logicalW, float logicalH) {
	if (!worldSet || !globe.isReady()) return;

	// Paint the backdrop primitives (starfield) beneath the blitted globe; the
	// scrim and all foreground UI batch after it and composite on top. Render
	// even at fade 0 so the colorizer's async uploads keep pumping.
	Renderer::Primitives::flush();
	globe.render(rect, logicalW, logicalH, fade * kDiscOpacity);

	if (fade <= 0.0F) return;

	// Screen-level bg_void -> transparent scrim: solid over the left quarter
	// (the identity/menu column), easing out by ~2/3 across, so the disc
	// emerges from shadow and foreground text stays legible.
	const float				solidX = logicalW * kScrimSolidFrac;
	const float				clearX = logicalW * kScrimFadeFrac;
	const float				alpha = kScrimAlpha * fade;
	const Foundation::Color scrimEdge{UI::bg_void.r, UI::bg_void.g, UI::bg_void.b, alpha};
	const Foundation::Color scrimClear{UI::bg_void.r, UI::bg_void.g, UI::bg_void.b, 0.0F};
	Renderer::Primitives::drawRect(Renderer::Primitives::RectArgs{
		.bounds = {0.0F, 0.0F, solidX, logicalH},
		.style = {.fill = scrimEdge}});
	const Foundation::Vec2	v[4] = {
		 {solidX, 0.0F}, {clearX, 0.0F}, {clearX, logicalH}, {solidX, logicalH}};
	const uint16_t			idx[6] = {0, 1, 2, 0, 2, 3};
	const Foundation::Color colors[4] = {scrimEdge, scrimClear, scrimClear, scrimEdge};
	Renderer::Primitives::drawTriangles(Renderer::Primitives::TrianglesArgs{
		.vertices = v, .indices = idx, .vertexCount = 4, .indexCount = 6, .colors = colors});
}

} // namespace world_sim
