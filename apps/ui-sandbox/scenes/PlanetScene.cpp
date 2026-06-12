// Planet Scene - 3D Globe Viewer (M3f milestone)
// Renders a procedurally generated planet as a shaded 3D globe using the real
// worldgen pipeline. Color modes 0-6 (right-click cycles); drag to orbit; scroll to zoom; click to pick.

#include "SceneTypes.h"

#include <planet-view/OrbitCamera.h>
#include <planet-view/PlanetColorizer.h>
#include <planet-view/PlanetDetailCache.h>
#include <planet-view/PlanetMesh.h>
#include <planet-view/PlanetPicker.h>
#include <planet-view/PlanetRenderer.h>
#include <planet-view/PlanetScheduler.h>
#include <world/worldgen/data/PlanetParams.h>
#include <world/worldgen/pipeline/PlanetGenerator.h>

#include <threading/TaskPool.h>

#include <graphics/Color.h>
#include <primitives/Primitives.h>
#include <scene/Scene.h>
#include <scene/SceneManager.h>
#include <utils/Log.h>
#include <utils/ResourcePath.h>

#include <GL/glew.h>
#include <input/InputTypes.h>
#include <ui/input/InputEvent.h>

#include <cstdio>
#include <memory>
#include <optional>
#include <string>

namespace {

constexpr const char* kSceneName = "planet";
// 256 tiles/edge gives 655k tiles — full Earth-like detail with the stub pipeline.
constexpr uint32_t kSubdivision = 256;

class PlanetScene : public engine::IScene {
  public:
    void onEnter() override {
        timeSinceEnter = 0.0F;
        LOG_INFO(World, "PlanetScene: starting planet generation (n=%u)", kSubdivision);
        statusText = "Generating planet...";

        worldgen::PlanetParams params = worldgen::PlanetParams::preset(worldgen::Preset::EarthLike);
        params.gridSubdivision = kSubdivision;
        params.seed = 42;

        generator = std::make_unique<worldgen::PlanetGenerator>();
        generator->start(params);
    }

    void update(float dt) override {
        timeSinceEnter += dt;
        camera.update(dt);

        if (!generator) return;

        auto prog = generator->progress();

        // Update progress text.
        if (prog.state == worldgen::GenerationProgress::State::Running) {
            char buf[128];
            snprintf(buf, sizeof(buf), "Generating... %.0f%%  [%s]",
                     static_cast<double>(prog.totalFraction * 100.0f),
                     prog.stageName ? prog.stageName : "");
            statusText = buf;
        }

        // Check for new snapshot and upload colors progressively.
        auto snap = generator->snapshot();
        if (snap && snap != lastSnapshot) {
            lastSnapshot = snap;
            onSnapshot(*snap);
        }

        // Generation complete.
        if (prog.state == worldgen::GenerationProgress::State::Complete && !worldReady) {
            worldReady = true;
            statusText = "";
            LOG_INFO(World, "PlanetScene: generation complete");
        }

        if (prog.state == worldgen::GenerationProgress::State::Failed && !worldReady) {
            statusText = "ERROR: generation failed";
        }
    }

    void render() override {
        int vpW = 0, vpH = 0;
        Renderer::Primitives::getLogicalViewport(vpW, vpH);

        glClearColor(0.0F, 0.0F, 0.02F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT);

        if (meshBuilt && renderer.isReady() && colorizer.isReady()) {
            GLint vp[4] = {};
            glGetIntegerv(GL_VIEWPORT, vp);
            int fbW = vp[2], fbH = vp[3];

            colorizer.uploadPending();
            if (lastSnapshot && lastSnapshot->grid && detailCache.isReady()) {
                float aspect = (fbH > 0) ? (static_cast<float>(fbW) / fbH) : 1.0F;
                planetview::schedulePages(detailCache, camera, aspect, fbW, fbH,
                                          *lastSnapshot->grid, kSubdivision);
            }

            renderer.render(mesh, colorizer, detailCache, kSubdivision, camera, fbW, fbH);
            renderer.blitToScreen(fbW, fbH);

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

        if (!statusText.empty()) {
            Renderer::Primitives::drawText({
                .text     = statusText,
                .position = {20.0F, 20.0F},
                .scale    = 1.0F,
                .color    = Foundation::Color(1.0F, 1.0F, 1.0F, 1.0F)
            });
        }

        if (meshBuilt) {
            std::string modeLabel = std::string("Mode: ") +
                planetview::colorModeName(static_cast<planetview::ColorMode>(colorModeIdx));
            modeLabel += "  [right-click to cycle]";
            Renderer::Primitives::drawText({
                .text     = modeLabel,
                .position = {20.0F, 48.0F},
                .scale    = 0.8F,
                .color    = Foundation::Color(0.9F, 0.9F, 0.9F, 0.9F)
            });
        }
    }

    void onExit() override {
        if (generator) {
            generator->cancel();
            generator.reset();
        }
    }

    bool handleInput(UI::InputEvent& event) override {
        // Ignore all mouse input for the first 0.25s after scene activation.
        // Synthetic/buffered mouse events from scene switching can otherwise
        // fire spurious right-clicks that cycle the color mode at startup.
        if (timeSinceEnter < 0.25F &&
            (event.type == UI::InputEvent::Type::MouseDown ||
             event.type == UI::InputEvent::Type::MouseUp   ||
             event.type == UI::InputEvent::Type::MouseMove)) {
            return false;
        }

        if (event.type == UI::InputEvent::Type::MouseDown &&
            event.button == engine::MouseButton::Left) {

            if (meshBuilt && lastSnapshot) {
                int vpW = 0, vpH = 0;
                Renderer::Primitives::getLogicalViewport(vpW, vpH);
                float aspect = (vpH > 0) ? (static_cast<float>(vpW) / static_cast<float>(vpH)) : 1.0F;

                float ndcX =  (event.position.x / static_cast<float>(vpW)) * 2.0F - 1.0F;
                float ndcY = -(event.position.y / static_cast<float>(vpH)) * 2.0F + 1.0F;

                auto latlon = planetview::pick(camera, aspect, ndcX, ndcY);
                if (latlon) {
                    markerPos     = planetview::latLonToUnitSphere(latlon->latDeg, latlon->lonDeg);
                    markerVisible = true;

                    // Log tile at picked location using the real grid.
                    worldgen::Vec3d dir{markerPos.x, markerPos.y, markerPos.z};
                    worldgen::TileId t = lastSnapshot->grid->fromUnitVector(dir);
                    LOG_INFO(World, "Picked: lat=%.2f lon=%.2f tileId=%u",
                             static_cast<double>(latlon->latDeg),
                             static_cast<double>(latlon->lonDeg),
                             t);
                }
            }
            camera.beginDrag(event.position.x, event.position.y);
            draggingLeft = true;
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

        // Right-click cycles color mode (key events not available in this input system).
        if (event.type == UI::InputEvent::Type::MouseDown &&
            event.button == engine::MouseButton::Right) {
            switchMode((colorModeIdx + 1) % static_cast<int>(planetview::ColorMode::Count));
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
    float timeSinceEnter{0.0F};

    std::unique_ptr<worldgen::PlanetGenerator> generator;
    std::shared_ptr<const worldgen::GeneratedWorld> lastSnapshot;
    bool worldReady{false};
    bool meshBuilt{false};
    std::string statusText;

    // pool declared first so it outlives colorizer/detailCache (they bake on it
    // and wait on those bakes in their destructors).
    foundation::TaskPool        pool;
    planetview::PlanetMesh      mesh;
    planetview::PlanetColorizer colorizer;
    planetview::PlanetDetailCache detailCache;
    planetview::PlanetRenderer  renderer;
    planetview::OrbitCamera     camera;

    int  colorModeIdx{static_cast<int>(planetview::ColorMode::Terrain)};
    bool draggingLeft{false};

    glm::vec3 markerPos{1.0F, 0.0F, 0.0F};
    bool      markerVisible{false};

    // Called each time a new snapshot arrives (progressive phases).
    void onSnapshot(const worldgen::GeneratedWorld& world) {
        if (!meshBuilt && world.grid) {
            // Build mesh on first snapshot so vertices use the real grid.
            mesh.build(kSubdivision, *world.grid);

            colorizer.init(kSubdivision);
            detailCache.init(kSubdivision);

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

            meshBuilt = true;
            LOG_INFO(World, "PlanetScene: mesh and renderer ready");
        }

        // Re-bake colors whenever validFields grows (progressive stages).
        if (meshBuilt && colorizer.isReady() && lastSnapshot) {
            auto mode = static_cast<planetview::ColorMode>(colorModeIdx);
            colorizer.requestBake(lastSnapshot, mode, pool);
            detailCache.setWorld(lastSnapshot, mode);
        }
    }

    void switchMode(int idx) {
        colorModeIdx = idx % static_cast<int>(planetview::ColorMode::Count);
        if (lastSnapshot && colorizer.isReady()) {
            auto mode = static_cast<planetview::ColorMode>(colorModeIdx);
            colorizer.requestBake(lastSnapshot, mode, pool);
            detailCache.setWorld(lastSnapshot, mode);
        }
    }
};

} // anonymous namespace

namespace ui_sandbox::scenes {
    extern const ui_sandbox::SceneInfo Planet = {
        kSceneName,
        []() {
            return std::make_unique<PlanetScene>();
        }
    };
} // namespace ui_sandbox::scenes
