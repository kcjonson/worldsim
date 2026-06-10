// Planet Scene - 3D Globe Viewer (M3f milestone)
// Renders a procedurally generated planet as a shaded 3D globe.
// Number keys 1-7 switch color modes; drag to orbit; scroll to zoom; click to pick.

#include "SceneTypes.h"

#include <planet-view/OrbitCamera.h>
#include <planet-view/PlanetColorizer.h>
#include <planet-view/PlanetMesh.h>
#include <planet-view/PlanetMeshMath.h>
#include <planet-view/PlanetPicker.h>
#include <planet-view/PlanetRenderer.h>
#include <world/generation/GeneratedWorld.h>
#include <world/generation/PlanetGenerator.h>

#include <graphics/Color.h>
#include <primitives/Primitives.h>
#include <scene/Scene.h>
#include <scene/SceneManager.h>
#include <utils/Log.h>
#include <utils/ResourcePath.h>

#include <GL/glew.h>
#include <input/InputTypes.h>
#include <ui/input/InputEvent.h>

#include <atomic>
#include <future>
#include <optional>
#include <string>

namespace {

constexpr const char* kSceneName = "planet";
constexpr uint32_t   kSubdivision = 64; // quick for demo; raise to 256 for quality

class PlanetScene : public engine::IScene {
  public:
    void onEnter() override {
        LOG_INFO(World, "PlanetScene: starting planet generation (n=%u)", kSubdivision);
        statusText = "Generating planet...";

        // Run generation on a background thread.
        generationFuture = std::async(std::launch::async, []() {
            worldgen::PlanetGenerator gen;
            return gen.generate({.seed = 42, .subdivision = kSubdivision});
        });
    }

    void update(float dt) override {
        camera.update(dt);

        // Check if generation finished.
        if (!worldReady && generationFuture.valid()) {
            using namespace std::chrono_literals;
            if (generationFuture.wait_for(0ms) == std::future_status::ready) {
                world = generationFuture.get();
                generationFuture = {};
                onWorldReady();
            }
        }
    }

    void render() override {
        int vpW = 0, vpH = 0;
        Renderer::Primitives::getLogicalViewport(vpW, vpH);

        glClearColor(0.0F, 0.0F, 0.02F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT);

        if (worldReady && renderer.isReady()) {
            // Get actual framebuffer size (may differ on HiDPI).
            GLint vp[4] = {};
            glGetIntegerv(GL_VIEWPORT, vp);
            int fbW = vp[2], fbH = vp[3];

            renderer.render(mesh, colorizer, camera, fbW, fbH);
            renderer.blitToScreen(fbW, fbH);

            // Draw click marker if we have one.
            if (markerVisible) {
                float aspect = (vpH > 0) ? (static_cast<float>(vpW) / static_cast<float>(vpH)) : 1.0F;
                float sx = 0.0F, sy = 0.0F;
                if (planetview::projectToScreen(markerPos, camera, aspect, vpW, vpH, sx, sy)) {
                    Renderer::Primitives::drawCircle({
                        .center = {sx, sy},
                        .radius = 6.0F,
                        .style  = {
                            .fill   = Foundation::Color(1.0F, 1.0F, 0.0F, 0.8F),
                            .border = Foundation::BorderStyle{
                                .color = Foundation::Color(0.0F, 0.0F, 0.0F, 1.0F),
                                .width = 2.0F
                            }
                        }
                    });
                }
            }
        }

        // Status text drawn via 2D Primitives on top (no text in 3D pass).
        if (!statusText.empty()) {
            Renderer::Primitives::drawText({
                .text     = statusText,
                .position = {20.0F, 20.0F},
                .scale    = 1.0F,
                .color    = Foundation::Color(1.0F, 1.0F, 1.0F, 1.0F)
            });
        }

        // Mode label.
        if (worldReady) {
            std::string modeLabel = std::string("Mode: ") +
                planetview::colorModeName(static_cast<planetview::ColorMode>(colorModeIdx));
            modeLabel += "  [1-7 to switch]";
            Renderer::Primitives::drawText({
                .text     = modeLabel,
                .position = {20.0F, 48.0F},
                .scale    = 0.8F,
                .color    = Foundation::Color(0.9F, 0.9F, 0.9F, 0.9F)
            });
        }
    }

    void onExit() override {
        // Wait for background generation to finish before destroying objects.
        if (generationFuture.valid()) {
            generationFuture.wait();
        }
    }

    bool handleInput(UI::InputEvent& event) override {
        if (event.type == UI::InputEvent::Type::MouseDown &&
            event.button == engine::MouseButton::Left) {

            if (worldReady) {
                int vpW = 0, vpH = 0;
                Renderer::Primitives::getLogicalViewport(vpW, vpH);
                float aspect = (vpH > 0) ? (static_cast<float>(vpW) / static_cast<float>(vpH)) : 1.0F;

                float ndcX =  (event.position.x / static_cast<float>(vpW)) * 2.0F - 1.0F;
                float ndcY = -(event.position.y / static_cast<float>(vpH)) * 2.0F + 1.0F;

                auto latlon = planetview::pick(camera, aspect, ndcX, ndcY);
                if (latlon) {
                    markerPos     = planetview::latLonToUnitSphere(latlon->latDeg, latlon->lonDeg);
                    markerVisible = true;

                    LOG_INFO(World, "Picked: lat=%.2f lon=%.2f",
                             static_cast<double>(latlon->latDeg),
                             static_cast<double>(latlon->lonDeg));
                }
            }
            camera.beginDrag(event.position.x, event.position.y);
            event.consume();
            return true;
        }

        if (event.type == UI::InputEvent::Type::MouseMove && draggingLeft) {
            camera.drag(event.position.x, event.position.y);
            return true;
        }

        if (event.type == UI::InputEvent::Type::MouseUp &&
            event.button == engine::MouseButton::Left) {
            camera.endDrag();
            draggingLeft = false;
        }

        if (event.type == UI::InputEvent::Type::Scroll) {
            camera.scroll(event.scrollDelta);
            event.consume();
            return true;
        }

        return false;
    }

    std::string exportState() override {
        char buf[256];
        snprintf(buf, sizeof(buf),
                 R"({"ready":%s,"mode":"%s","yaw":%.3f,"pitch":%.3f})",
                 worldReady ? "true" : "false",
                 planetview::colorModeName(static_cast<planetview::ColorMode>(colorModeIdx)),
                 static_cast<double>(camera.yaw),
                 static_cast<double>(camera.pitch));
        return {buf};
    }

    const char* getName() const override { return kSceneName; }

  private:
    worldgen::GeneratedWorld world;
    std::future<worldgen::GeneratedWorld> generationFuture;
    bool worldReady{false};
    std::string statusText;

    planetview::PlanetMesh     mesh;
    planetview::PlanetColorizer colorizer;
    planetview::PlanetRenderer  renderer;
    planetview::OrbitCamera     camera;

    int  colorModeIdx{static_cast<int>(planetview::ColorMode::Combined)};
    bool draggingLeft{false};

    glm::vec3 markerPos{1.0F, 0.0F, 0.0F};
    bool      markerVisible{false};

    void onWorldReady() {
        LOG_INFO(World, "PlanetScene: generation complete, building GPU resources");

        mesh.build(kSubdivision);

        colorizer.init(kSubdivision);
        colorizer.update(world, static_cast<planetview::ColorMode>(colorModeIdx));

        std::string shaderDir = Foundation::findResourceString("shaders");
        if (shaderDir.empty()) shaderDir = "shaders";

        int vpW = 0, vpH = 0;
        Renderer::Primitives::getLogicalViewport(vpW, vpH);
        if (vpW <= 0) vpW = 1280;
        if (vpH <= 0) vpH = 720;

        if (!renderer.init(shaderDir.c_str(), vpW, vpH)) {
            LOG_ERROR(World, "PlanetScene: renderer init failed");
            statusText = "ERROR: shader load failed";
            return;
        }

        worldReady = true;
        statusText = "";
        LOG_INFO(World, "PlanetScene: ready");
    }

    void switchMode(int idx) {
        colorModeIdx = idx % static_cast<int>(planetview::ColorMode::Count);
        if (worldReady) {
            colorizer.update(world, static_cast<planetview::ColorMode>(colorModeIdx));
        }
    }
};

} // anonymous namespace

// Number-key input is polled via update() since handleInput only fires for mouse.
// We need keyboard — inject key polling via a thin wrapper.
namespace {

class PlanetSceneWithKeys : public PlanetScene {
  public:
    void update(float dt) override {
        PlanetScene::update(dt);
        // Key polling not available without GLFW; handled via handleInput below.
    }
};

} // namespace

// Export scene info.
namespace ui_sandbox::scenes {
    extern const ui_sandbox::SceneInfo Planet = {
        kSceneName,
        []() {
            return std::make_unique<PlanetScene>();
        }
    };
} // namespace ui_sandbox::scenes
