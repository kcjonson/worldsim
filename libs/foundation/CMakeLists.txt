add_library(foundation
    metrics/performance_metrics.cpp
    debug/debug_server.cpp
)

target_include_directories(foundation
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(foundation PUBLIC cxx_std_20)

# Link dependencies
target_link_libraries(foundation
    PUBLIC
        glm::glm
        httplib::httplib
)

# Tests
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
