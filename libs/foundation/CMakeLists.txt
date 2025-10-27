add_library(foundation
    metrics/performance_metrics.cpp
    debug/debug_server.cpp
    utils/log.cpp
)

target_include_directories(foundation
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(foundation PUBLIC cxx_std_20)

# Define DEVELOPMENT_BUILD for Debug and RelWithDebInfo builds
# This enables Debug, Info, and Warning log levels
if(CMAKE_BUILD_TYPE MATCHES "Debug|RelWithDebInfo")
    target_compile_definitions(foundation PUBLIC DEVELOPMENT_BUILD)
endif()

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
