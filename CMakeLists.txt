cmake_minimum_required(VERSION 3.20)
project(world-sim VERSION 0.1.0 LANGUAGES CXX)

# Set C++20 standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find required packages
find_package(OpenGL REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(GLEW REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(httplib CONFIG REQUIRED)

# Enable testing (must be before subdirectories)
include(CTest)
enable_testing()

# Add subdirectories in dependency order
# Foundation layer
add_subdirectory(libs/foundation)

# Graphics layer
add_subdirectory(libs/renderer)

# Feature layers
add_subdirectory(libs/ui)
add_subdirectory(libs/world)
add_subdirectory(libs/game-systems)

# Engine layer
add_subdirectory(libs/engine)

# Applications
add_subdirectory(apps/world-sim)
add_subdirectory(apps/ui-sandbox)

# Build tools (atlas generator, etc.)
add_subdirectory(tools/generate_sdf_atlas)

# Developer client (web app)
# Only build in Development mode (builds React app with npm)
if(CMAKE_BUILD_TYPE STREQUAL "Development" OR CMAKE_BUILD_TYPE STREQUAL "Debug")
    # Check if npm is available
    find_program(NPM_EXECUTABLE npm)
    if(NPM_EXECUTABLE)
        message(STATUS "Building developer-client web app...")

        # Build web app during make
        add_custom_target(developer-client ALL
            COMMAND ${NPM_EXECUTABLE} install
            COMMAND ${NPM_EXECUTABLE} run build
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/apps/developer-client
            COMMENT "Building developer client web app with Vite"
        )

        # Copy built files to output directory
        add_custom_command(TARGET developer-client POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_SOURCE_DIR}/apps/developer-client/dist/
                ${CMAKE_BINARY_DIR}/developer-client/
            COMMENT "Copying developer client to build/developer-client/"
        )

        # Make applications depend on developer-client so it's always built first
        add_dependencies(world-sim developer-client)
        add_dependencies(ui-sandbox developer-client)
    else()
        message(WARNING "npm not found - skipping developer-client build. Install Node.js to enable developer tools.")
    endif()
endif()
