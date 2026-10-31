add_library(sim
    ecs/systems/TimeSystem.cpp
    ecs/systems/MovementSystem.cpp
    ecs/systems/PhysicsSystem.cpp
    ecs/systems/NeedsDecaySystem.cpp
    ecs/systems/VisionSystem.cpp
    ecs/systems/AIDecisionSystem.cpp
    ecs/systems/ActionSystem.cpp
    ecs/systems/action/NeedActions.cpp
    ecs/systems/action/HarvestActions.cpp
    ecs/systems/action/CraftActions.cpp
    ecs/systems/action/HaulActions.cpp
    ecs/systems/action/BuildActions.cpp
    ecs/systems/StorageGoalSystem.cpp
    ecs/systems/CraftingGoalSystem.cpp
    ecs/systems/BuildGoalSystem.cpp
    ecs/systems/ConstructionSystem.cpp
    ecs/systems/RoomDetectionSystem.cpp
    ecs/systems/CollisionSystem.cpp
    ecs/systems/WallCollisionSystem.cpp
    ecs/systems/StaticRectCollisionSystem.cpp
    ecs/systems/NavigationSystem.cpp
    ecs/spatial/AgentSpatialHash.cpp
    ecs/GoalTaskRegistry.cpp
    ecs/components/MemoryQueries.cpp
    ecs/components/ToiletLocationFinder.cpp
    construction/ConstructionWorld.cpp
    construction/ConstructionValidator.cpp
    construction/OpeningGeometry.cpp
    construction/RoomDetection.cpp
    construction/RoomHitTest.cpp
    construction/SnapEngine.cpp
    nav/NavInputBuilder.cpp
    vision/GeometryIndex.cpp
)

target_include_directories(sim
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(sim PUBLIC cxx_std_20)

# The simulation: the ECS and its systems, navigation, vision and construction. It links
# neither renderer nor ui itself; assets still brings salal::renderer (its vector module).
target_link_libraries(sim
    PUBLIC
        salal::foundation
        geometry
        world
        assets
)

# Tests
if(BUILD_TESTING)
    file(GLOB_RECURSE SIM_TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.test.cpp"
    )

    if(SIM_TEST_SOURCES)
        find_package(GTest CONFIG REQUIRED)
        add_executable(sim-tests ${SIM_TEST_SOURCES})
        target_link_libraries(sim-tests
            PRIVATE
                sim
                GTest::gtest
                GTest::gtest_main
        )
        if(MSVC)
            target_compile_options(sim-tests PRIVATE /Od)
        else()
            target_compile_options(sim-tests PRIVATE -O0 -g)
        endif()
        add_test(NAME sim-tests COMMAND sim-tests)
    endif()
endif()
