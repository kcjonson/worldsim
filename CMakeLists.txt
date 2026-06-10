cmake_minimum_required(VERSION 3.20)
project(world-sim VERSION 0.1.0 LANGUAGES CXX)

# Set C++20 standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Treat out-of-order designated initializers as an error where supported.
# MSVC enforces C++20 declaration-order for designators (C7560); enabling the
# matching warning-as-error on Clang/GCC catches the same portability breaks
# locally. Feature-detect the flag so this is safe on AppleClang and on
# compilers that don't recognize the warning group (e.g. some GCC versions).
if(MSVC)
    # Sources are UTF-8 (authored on macOS/Clang). Tell MSVC so Unicode character
    # literals compile instead of erroring as multibyte constants (C2015).
    add_compile_options(/utf-8)
else()
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag("-Werror=reorder-init-list" HAS_WERROR_REORDER_INIT_LIST)
    if(HAS_WERROR_REORDER_INIT_LIST)
        add_compile_options(-Werror=reorder-init-list)
    endif()
endif()

# Force use of LLVM's libc++ on macOS
if(APPLE)
    add_link_options("-L/usr/local/opt/llvm/lib/c++" "-Wl,-rpath,/usr/local/opt/llvm/lib/c++")
endif()

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
add_subdirectory(libs/planet-view)
add_subdirectory(libs/game-systems)

# Engine layer
add_subdirectory(libs/engine)

# Asset system (depends on foundation, renderer)
add_subdirectory(libs/engine/assets)

# Applications
add_subdirectory(apps/world-sim)
add_subdirectory(apps/ui-sandbox)

# Build tools (atlas generator, etc.)
add_subdirectory(tools/generate_sdf_atlas)

# Generate the SDF font atlas as a build step so a fresh checkout runs without a
# manual tool invocation. The PNG is a gitignored build artifact, rebuilt when the
# source font changes; the JSON metadata regenerates deterministically.
add_custom_command(
    OUTPUT ${CMAKE_SOURCE_DIR}/fonts/Roboto-SDF.png ${CMAKE_SOURCE_DIR}/fonts/Roboto-SDF.json
    COMMAND $<TARGET_FILE:generate_sdf_atlas>
        --font ${CMAKE_SOURCE_DIR}/fonts/Roboto-Regular.ttf
        --output ${CMAKE_SOURCE_DIR}/fonts/Roboto-SDF.png
        --metadata ${CMAKE_SOURCE_DIR}/fonts/Roboto-SDF.json
    DEPENDS generate_sdf_atlas ${CMAKE_SOURCE_DIR}/fonts/Roboto-Regular.ttf
    COMMENT "Generating SDF font atlas (fonts/Roboto-SDF.png)"
    VERBATIM
)
add_custom_target(font-atlas
    DEPENDS ${CMAKE_SOURCE_DIR}/fonts/Roboto-SDF.png ${CMAKE_SOURCE_DIR}/fonts/Roboto-SDF.json)
add_dependencies(world-sim font-atlas)
add_dependencies(ui-sandbox font-atlas)

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
