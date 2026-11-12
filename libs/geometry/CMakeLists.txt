add_library(geometry
    predicates/Predicates.cpp
    polygon/Polygon.cpp
    arrangement/Arrangement.cpp
    arrangement/HalfEdge.cpp
    offset/WallOffset.cpp
    boolean/RingBoolean.cpp
    contour/CatmullRom.cpp
    contour/ClipRing.cpp
    contour/MarchingSquares.cpp
    contour/RingPins.cpp
    contour/Smoothing.cpp
    contour/Stroke.cpp
    contour/WarpField.cpp
    silhouette/Silhouette.cpp
    triangulation/ConstrainedDelaunay.cpp
    visibility/Visibility.cpp
    nav/MeshComponents.cpp
    nav/NavMesh.cpp
    nav/PathQuery.cpp
    nav/PatchableMesh.cpp
)

target_include_directories(geometry
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(geometry PUBLIC cxx_std_20)

target_link_libraries(geometry
    PUBLIC
        salal::foundation
)

# Tests
if(BUILD_TESTING)
    file(GLOB_RECURSE GEOMETRY_TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.test.cpp"
    )

    if(GEOMETRY_TEST_SOURCES)
        find_package(GTest CONFIG REQUIRED)
        add_executable(geometry-tests ${GEOMETRY_TEST_SOURCES})
        target_link_libraries(geometry-tests
            PRIVATE
                geometry
                GTest::gtest
                GTest::gtest_main
        )
        if(MSVC)
            target_compile_options(geometry-tests PRIVATE /Od)
        else()
            target_compile_options(geometry-tests PRIVATE -O0 -g)
        endif()

        add_test(NAME geometry-tests COMMAND geometry-tests)
    endif()

    file(GLOB_RECURSE GEOMETRY_BENCHMARK_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.bench.cpp"
    )

    if(GEOMETRY_BENCHMARK_SOURCES)
        find_package(benchmark CONFIG REQUIRED)
        add_executable(geometry-benchmarks ${GEOMETRY_BENCHMARK_SOURCES})
        target_link_libraries(geometry-benchmarks
            PRIVATE
                geometry
                benchmark::benchmark
                benchmark::benchmark_main
        )
        # MSVC: per-config defaults (/O2 in Release); forcing /O2 collides with
        # Debug's /RTC1 (D8016).
        if(NOT MSVC)
            target_compile_options(geometry-benchmarks PRIVATE -O3)
        endif()
        add_test(NAME geometry-benchmarks COMMAND geometry-benchmarks)
    endif()
endif()
