add_library(world
    generation/placeholder.cpp
)

target_include_directories(world
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(world PUBLIC cxx_std_20)

# Link dependencies
target_link_libraries(world
    PUBLIC
        foundation
        renderer
        nlohmann_json::nlohmann_json
)

# Tests
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
