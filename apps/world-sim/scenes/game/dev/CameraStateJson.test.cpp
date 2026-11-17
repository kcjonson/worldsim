#include "scenes/game/dev/CameraStateJson.h"

#include <world/camera/WorldCamera.h>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace {

	constexpr float kPixelsPerMeter = 16.0F;
	constexpr int	kWindowWidth = 1280;
	constexpr int	kWindowHeight = 720;

	world::WorldCamera cameraAt(float x, float y, float zoom) {
		world::WorldCamera camera;
		camera.setPosition({x, y});
		camera.setZoom(zoom);
		return camera;
	}

} // namespace

TEST(CameraStateJson, ReportsTheCenterZoomScaleWindowAndVisibleRectangle) {
	const std::string json = world_sim::cameraStateJson(cameraAt(100.0F, -40.0F, 2.0F), kPixelsPerMeter, kWindowWidth, kWindowHeight);
	ASSERT_TRUE(nlohmann::json::accept(json)) << json;
	const auto parsed = nlohmann::json::parse(json);
	EXPECT_DOUBLE_EQ(parsed.at("center").at("x").get<double>(), 100.0);
	EXPECT_DOUBLE_EQ(parsed.at("center").at("y").get<double>(), -40.0);
	EXPECT_DOUBLE_EQ(parsed.at("zoom").get<double>(), 2.0);
	EXPECT_DOUBLE_EQ(parsed.at("pixelsPerMeter").get<double>(), 32.0);
	EXPECT_EQ(parsed.at("window").at("width").get<int>(), 1280);
	EXPECT_EQ(parsed.at("window").at("height").get<int>(), 720);
	EXPECT_DOUBLE_EQ(parsed.at("visible").at("minX").get<double>(), 80.0);
	EXPECT_DOUBLE_EQ(parsed.at("visible").at("maxX").get<double>(), 120.0);
	EXPECT_DOUBLE_EQ(parsed.at("visible").at("minY").get<double>(), -51.25);
	EXPECT_DOUBLE_EQ(parsed.at("visible").at("maxY").get<double>(), -28.75);
}

// The README's two formulas, with y running down the screen as it does in the world:
// pixelX = width / 2 + (worldX - centerX) * pixelsPerMeter, pixelY = height / 2 + (worldY - centerY) * pixelsPerMeter.
TEST(CameraStateJson, TheReadmeFormulasMatchTheCamerasOwnTransform) {
	const world::WorldCamera camera = cameraAt(100.0F, -40.0F, 2.0F);
	const auto	 parsed = nlohmann::json::parse(world_sim::cameraStateJson(camera, kPixelsPerMeter, kWindowWidth, kWindowHeight));
	const double scale = parsed.at("pixelsPerMeter").get<double>();
	const double centerX = parsed.at("center").at("x").get<double>();
	const double centerY = parsed.at("center").at("y").get<double>();
	const double width = parsed.at("window").at("width").get<double>();
	const double height = parsed.at("window").at("height").get<double>();
	const double pixelX = width / 2.0 + (103.5 - centerX) * scale;
	const double pixelY = height / 2.0 + (-45.25 - centerY) * scale;
	const auto	 screen = camera.worldToScreen(103.5F, -45.25F, kWindowWidth, kWindowHeight, kPixelsPerMeter);
	EXPECT_NEAR(pixelX, screen.x, 1e-3);
	EXPECT_NEAR(pixelY, screen.y, 1e-3);
	const auto world =
		camera.screenToWorld(static_cast<float>(pixelX), static_cast<float>(pixelY), kWindowWidth, kWindowHeight, kPixelsPerMeter);
	EXPECT_NEAR(centerX + (pixelX - width / 2.0) / scale, world.x, 1e-3);
	EXPECT_NEAR(centerY + (pixelY - height / 2.0) / scale, world.y, 1e-3);
	EXPECT_NEAR(world.x, 103.5, 1e-3);
	EXPECT_NEAR(world.y, -45.25, 1e-3);
}
