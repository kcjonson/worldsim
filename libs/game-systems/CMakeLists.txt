add_library(game-systems
    chunks/placeholder.cpp
)

target_include_directories(game-systems
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(game-systems PUBLIC cxx_std_20)

# Link dependencies
target_link_libraries(game-systems
    PUBLIC
        foundation
        renderer
        world
)

# Tests
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
