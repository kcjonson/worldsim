add_library(geometry
    predicates/Predicates.cpp
    polygon/Polygon.cpp
    arrangement/Arrangement.cpp
    arrangement/HalfEdge.cpp
    offset/WallOffset.cpp
    boolean/RingBoolean.cpp
)

target_include_directories(geometry
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(geometry PUBLIC cxx_std_20)

target_link_libraries(geometry
    PUBLIC
        foundation
)

# Tests
if(BUILD_TESTING)
    file(GLOB_RECURSE GEOMETRY_TEST_SOURCES
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
            target_compile_options(geometry-tests PRIVATE /Od /Zi)
        else()
            target_compile_options(geometry-tests PRIVATE -O0 -g)
        endif()

        add_test(NAME geometry-tests COMMAND geometry-tests)
    endif()
endif()
