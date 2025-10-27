add_library(renderer
    primitives/primitives.cpp
    primitives/batch_renderer.cpp
    metrics/metrics_collector.cpp
)

target_include_directories(renderer
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(renderer PUBLIC cxx_std_20)

# Link dependencies
target_link_libraries(renderer
    PUBLIC
        foundation
        OpenGL::GL
        GLEW::GLEW
        glfw
        glm::glm
)

# Tests
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
