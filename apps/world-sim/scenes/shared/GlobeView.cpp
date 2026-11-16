#include "GlobeView.h"

#include <utils/Log.h>
#include <utils/ResourcePath.h>

#include <algorithm>
#include <string>

namespace world_sim {

void GlobeView::chooseMinDistance(uint32_t n) {
	// Pick the closest orbit so a single tile can reach ~kTargetPx on a 1080p view.
	// Pixels-per-tile is monotone in distance; solve for that at h=1080.
	// Closed form: pxPerTile = (1.1/n)/(d-1) * (h/fovRad); set = kTargetPx.
	constexpr float kTargetPx = 100.0f; // biggest a single hex gets on screen
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

void GlobeView::render(const Foundation::Rect& rect, const Renderer::Viewport& window, float alpha) {
	if (!isReady()) {
		return;
	}

	const float scale = window.pixelRatio();
	int px = static_cast<int>(rect.x * scale);
	int py = static_cast<int>(rect.y * scale);
	int pw = static_cast<int>(rect.width * scale);
	int ph = static_cast<int>(rect.height * scale);
	if (pw <= 0 || ph <= 0) return;

	// Pump async base-tier color uploads for the current view.
	colorizer.uploadPending();

	// GL viewport origin is bottom-left; UI rect origin is top-left
	renderer.render(mesh, colorizer,
	                currentWorld ? currentWorld->grid->subdivision() : 0,
	                camera,
	                {.x = px, .y = window.framebufferHeight - py - ph, .width = pw, .height = ph,
	                 .windowWidth = window.framebufferWidth, .windowHeight = window.framebufferHeight},
	                alpha);
}

bool GlobeView::handleInput(ui::PointerEvent& event, const Foundation::Rect& rect, bool cycleOnRightClick) {
	if (!isReady()) return false;

	switch (event.type) {
		case ui::PointerEvent::Type::Down:
			if (!contains(rect, event.screen))
				break;
			if (event.button == foundation::MouseButton::Left) {
				camera.beginDrag(event.screen.x, event.screen.y);
				dragging = true;
				event.consume();
				return true;
			}
			if (event.button == foundation::MouseButton::Right && cycleOnRightClick) {
				camera.markInteracted(); // a globe interaction: stop the reveal spin
				cycleColorMode();
				event.consume();
				return true;
			}
			break;

		case ui::PointerEvent::Type::Move:
			if (dragging) {
				camera.drag(event.screen.x, event.screen.y);
				return true;
			}
			break;

		case ui::PointerEvent::Type::Up:
		case ui::PointerEvent::Type::Cancel:
			if (dragging && (event.button == foundation::MouseButton::Left || event.type == ui::PointerEvent::Type::Cancel)) {
				camera.endDrag();
				dragging = false;
				event.consume();
				return true;
			}
			break;

		case ui::PointerEvent::Type::Wheel:
			// ui's wheel is px of content, positive down; the camera's is notches, positive in.
			if (contains(rect, event.screen)) {
				camera.scroll(-event.wheel.y / 48.0F);
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
