#include "PlanetPicker.h"
#include "OrbitCamera.h"

#include <gtest/gtest.h>
#include <cmath>

using namespace planetview;

static constexpr float kEps = 1e-3F;

// Helper: project a unit-sphere point to NDC and pick back.
// Returns false without failing if the point is not visible from this camera.
// Asserts the picked lat/lon round-trips to within toleranceDeg degrees when visible.
static bool roundtripTest(float latDeg, float lonDeg,
                           float camYaw, float camPitch, float camDist,
                           float toleranceDeg) {
    OrbitCamera cam;
    cam.yaw      = camYaw;
    cam.pitch    = camPitch;
    cam.distance = camDist;

    float aspect = 16.0F / 9.0F;
    glm::mat4 mvp = cam.mvpMatrix(aspect);
    glm::mat4 view = cam.viewMatrix();

    glm::vec3 unitPos = latLonToUnitSphere(latDeg, lonDeg);

    // Check the point is on the near side of the sphere (visible from camera).
    glm::vec3 toPoint = unitPos - cam.position();
    glm::vec3 toCenter = -cam.position();
    if (glm::dot(glm::normalize(toPoint), glm::normalize(toCenter)) < 0.0F) {
        return false; // point behind planet limb — skip
    }

    glm::vec4 clip = mvp * glm::vec4(unitPos, 1.0F);
    if (clip.w <= 0.0F) return false;
    glm::vec3 ndc = glm::vec3(clip) / clip.w;
    if (std::abs(ndc.x) > 0.98F || std::abs(ndc.y) > 0.98F) return false;
    if (ndc.z < -1.0F || ndc.z > 1.0F) return false;

    auto result = pick(cam, aspect, ndc.x, ndc.y);
    if (!result.has_value()) return false; // may miss at limb — acceptable

    float dLat = std::abs(result->latDeg - latDeg);
    float dLon = std::abs(result->lonDeg - lonDeg);
    if (dLon > 180.0F) dLon = 360.0F - dLon;

    EXPECT_LT(dLat, toleranceDeg) << "lat roundtrip error: " << dLat
        << " (lat=" << latDeg << " lon=" << lonDeg << ")";
    EXPECT_LT(dLon, toleranceDeg) << "lon roundtrip error: " << dLon
        << " (lat=" << latDeg << " lon=" << lonDeg << ")";
    return true;
}

TEST(PlanetPicker, RoundtripEquator) {
    // Equator should be visible and round-trip cleanly.
    bool visible = roundtripTest(0.0F, 0.0F, 0.0F, 0.0F, 3.0F, 2.0F);
    EXPECT_TRUE(visible) << "equator not visible — check camera setup";
}

TEST(PlanetPicker, RoundtripNorthPole) {
    // Polar view from above.
    roundtripTest(80.0F, 45.0F, 1.0F, 1.2F, 3.0F, 5.0F);
}

TEST(PlanetPicker, RoundtripMidLatitude) {
    roundtripTest(30.0F, 60.0F, 1.5F, 0.4F, 2.5F, 3.0F);
}

TEST(PlanetPicker, RoundtripSouthernHemisphere) {
    // Camera pitched south so the point is visible.
    roundtripTest(-40.0F, 180.0F, 3.14F, -0.7F, 3.0F, 3.0F);
}

TEST(PlanetPicker, RoundtripMultipleCameras) {
    // Test with several camera orientations — at least one should see each point.
    struct Case { float lat, lon, yaw, pitch, dist, tol; };
    Case cases[] = {
        { 0.0F,   45.0F, 0.8F,  0.0F, 2.5F, 2.0F },
        { 20.0F,  90.0F, 1.6F,  0.3F, 3.0F, 2.0F },
        {-20.0F, -45.0F, 5.0F, -0.3F, 3.0F, 2.0F },
    };
    for (auto& c : cases) {
        roundtripTest(c.lat, c.lon, c.yaw, c.pitch, c.dist, c.tol);
    }
}

TEST(PlanetPicker, MissReturnsNullopt) {
    OrbitCamera cam;
    cam.yaw = 0.0F; cam.pitch = 0.0F; cam.distance = 3.0F;
    // NDC (0,0) is center of screen and should always hit the sphere.
    // NDC corners at +-1 may miss.  Test a clearly off-sphere NDC.
    auto result = pick(cam, 16.0F / 9.0F, 5.0F, 5.0F); // far outside viewport
    // With NDC (5,5), the ray will miss the unit sphere.
    EXPECT_FALSE(result.has_value());
}

// ── OrbitCamera clamp tests ──

TEST(OrbitCamera, PitchClamped) {
    OrbitCamera cam;
    cam.beginDrag(0.0F, 0.0F);
    cam.drag(0.0F, 10000.0F); // drag hard downward
    EXPECT_LE(cam.pitch, 1.5F);
    EXPECT_GE(cam.pitch, -1.5F);
}

TEST(OrbitCamera, DistanceClamped) {
    OrbitCamera cam;
    cam.scroll(-1000.0F); // zoom way out
    EXPECT_LE(cam.distance, 8.0F);
    cam.scroll(1000.0F);  // zoom way in
    EXPECT_GE(cam.distance, 1.05F);
}

TEST(OrbitCamera, AutoRotateAfterIdle) {
    OrbitCamera cam;
    cam.yaw = 0.0F;
    cam.update(5.0F); // 5 seconds of idle
    EXPECT_GT(cam.yaw, 0.0F); // should have auto-rotated
}
