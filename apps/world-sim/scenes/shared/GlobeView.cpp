#include "GlobeView.h"

#include <GL/glew.h>

#include <planet-view/PlanetScheduler.h>
#include <primitives/Primitives.h>
#include <utils/Log.h>
#include <utils/ResourcePath.h>

#include <algorithm>
#include <string>

namespace world_sim {

void GlobeView::chooseMinDistance(uint32_t n) {
	// Pick the closest orbit so a single tile can reach ~50 px on a 1080p view.
	// estimatePixelsPerTile is monotone in distance; solve for ~50 px at h=1080.
	// Closed form: pxPerTile = (1.1/n)/(d-1) * (h/fovRad); set = 50.
	constexpr float kTargetPx = 50.0f;
	constexpr float kFovRad = 45.0f * 3.14159265f / 180.0f;
	float h = 1080.0f;
	float gap = (1.1f / static_cast<float>(n)) * (h / kFovRad) / kTargetPx;
	camera.setMinDistance(1.0f + gap);
}

void GlobeView::setWorld(std::shared_ptr<const worldgen::GeneratedWorld> world) {
	currentWorld = std::move(world);
	if (!currentWorld || !currentWorld->grid) {
		return;
	}

	if (builtGrid != currentWorld->grid.get()) {
		uint32_t subdiv = currentWorld->grid->subdivision();
		mesh.build(subdiv, *currentWorld->grid);
		colorizer.init(subdiv);
		detailCache.init(subdiv);
		chooseMinDistance(subdiv);
		builtGrid = currentWorld->grid.get();

		if (!renderer.isReady()) {
			std::string shaderDir = Foundation::findResourceString("shaders");
			if (shaderDir.empty()) shaderDir = "shaders";
			if (!renderer.init(shaderDir.c_str(), 256, 256)) {
				LOG_ERROR(Game, "GlobeView: planet renderer init failed");
				builtGrid = nullptr;
				return;
			}
		}
		LOG_INFO(Game, "GlobeView: globe mesh ready (n=%u)", subdiv);
	}

	refreshColors();
}

void GlobeView::refreshColors() {
	if (currentWorld && colorizer.isReady()) {
		colorizer.requestBake(currentWorld, mode, pool);
		detailCache.setWorld(currentWorld, mode);
	}
}

void GlobeView::setColorMode(planetview::ColorMode newMode) {
	mode = newMode;
	refreshColors();
}

void GlobeView::cycleColorMode() {
	int next = (static_cast<int>(mode) + 1) % static_cast<int>(planetview::ColorMode::Count);
	setColorMode(static_cast<planetview::ColorMode>(next));
}

bool GlobeView::isReady() const {
	return builtGrid != nullptr && mesh.isBuilt() && renderer.isReady() && colorizer.isReady();
}

void GlobeView::render(const Foundation::Rect& rect, float logicalW, float logicalH) {
	if (!isReady()) {
		return;
	}

	GLint vp[4] = {};
	glGetIntegerv(GL_VIEWPORT, vp);

	float sx = logicalW > 0.0F ? static_cast<float>(vp[2]) / logicalW : 1.0F;
	float sy = logicalH > 0.0F ? static_cast<float>(vp[3]) / logicalH : 1.0F;
	int px = static_cast<int>(rect.x * sx);
	int py = static_cast<int>(rect.y * sy);
	int pw = static_cast<int>(rect.width * sx);
	int ph = static_cast<int>(rect.height * sy);
	if (pw <= 0 || ph <= 0) return;

	// Pump async base-tier uploads and stream detail pages for the current view.
	colorizer.uploadPending();
	if (currentWorld && currentWorld->grid && detailCache.isReady()) {
		float aspect = static_cast<float>(pw) / static_cast<float>(ph);
		planetview::schedulePages(detailCache, camera, aspect, pw, ph,
		                          *currentWorld->grid,
		                          currentWorld->grid->subdivision());
	}

	renderer.render(mesh, colorizer, detailCache,
	                currentWorld ? currentWorld->grid->subdivision() : 0,
	                camera, pw, ph);

	// GL viewport origin is bottom-left; UI rect origin is top-left
	glViewport(px, vp[3] - py - ph, pw, ph);
	renderer.blitToScreen(pw, ph);
	glViewport(vp[0], vp[1], vp[2], vp[3]);
}

bool GlobeView::handleInput(UI::InputEvent& event, const Foundation::Rect& rect, bool cycleOnRightClick) {
	if (!isReady()) return false;

	switch (event.type) {
		case UI::InputEvent::Type::MouseDown:
			if (!contains(rect, event.position)) break;
			if (event.button == engine::MouseButton::Left) {
				camera.beginDrag(event.position.x, event.position.y);
				dragging = true;
				event.consume();
				return true;
			}
			if (event.button == engine::MouseButton::Right && cycleOnRightClick) {
				cycleColorMode();
				event.consume();
				return true;
			}
			break;

		case UI::InputEvent::Type::MouseMove:
			if (dragging) {
				camera.drag(event.position.x, event.position.y);
				return true;
			}
			break;

		case UI::InputEvent::Type::MouseUp:
			if (dragging && event.button == engine::MouseButton::Left) {
				camera.endDrag();
				dragging = false;
				event.consume();
				return true;
			}
			break;

		case UI::InputEvent::Type::Scroll:
			if (contains(rect, event.position)) {
				camera.scroll(event.scrollDelta);
				event.consume();
				return true;
			}
			break;

		default:
			break;
	}
	return false;
}

std::optional<planetview::LatLon> GlobeView::pick(Foundation::Vec2 pos, const Foundation::Rect& rect) const {
	if (!isReady() || !contains(rect, pos) || rect.width <= 0.0F || rect.height <= 0.0F) {
		return std::nullopt;
	}
	float aspect = rect.width / rect.height;
	float ndcX = ((pos.x - rect.x) / rect.width) * 2.0F - 1.0F;
	float ndcY = -((pos.y - rect.y) / rect.height) * 2.0F + 1.0F;
	return planetview::pick(camera, aspect, ndcX, ndcY);
}

bool GlobeView::projectLatLon(planetview::LatLon site, const Foundation::Rect& rect,
                              float& outX, float& outY) const {
	if (!isReady() || rect.height <= 0.0F) return false;
	float aspect = rect.width / rect.height;
	glm::vec3 unitPos = planetview::latLonToUnitSphere(site.latDeg, site.lonDeg);
	float localX = 0.0F;
	float localY = 0.0F;
	if (!planetview::projectToScreen(unitPos, camera, aspect,
	                                 static_cast<int>(rect.width), static_cast<int>(rect.height),
	                                 localX, localY)) {
		return false;
	}
	outX = rect.x + localX;
	outY = rect.y + localY;
	return true;
}

} // namespace world_sim
