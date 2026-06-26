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

# Geometry layer (pure math, depends only on foundation)
add_subdirectory(libs/geometry)

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
add_subdirectory(apps/worldgen-cli)
add_subdirectory(apps/asset-cli)
add_subdirectory(apps/asset-manager)

# Build tools (atlas generator, etc.)
add_subdirectory(tools/generate_sdf_atlas)

# Generate the SDF font atlases as a build step so a fresh checkout runs without a
# manual tool invocation. The PNGs are gitignored build artifacts, rebuilt when the
# source font changes; the JSON metadata regenerates deterministically.
#
# Each entry is "AtlasBaseName:SourceTTF". The atlas base names must match
# FontFamily / kAtlasBaseNames in libs/renderer + libs/ui.
set(SDF_FONT_ATLASES
    "Roboto-SDF:Roboto-Regular.ttf"
    "ChakraPetch-SDF:ChakraPetch-SemiBold.ttf"
    "Barlow-SDF:Barlow-Regular.ttf"
    "JetBrainsMono-SDF:JetBrainsMono-Regular.ttf"
)

set(SDF_ATLAS_OUTPUTS "")
foreach(atlas_entry ${SDF_FONT_ATLASES})
    string(REPLACE ":" ";" atlas_parts ${atlas_entry})
    list(GET atlas_parts 0 atlas_name)
    list(GET atlas_parts 1 atlas_ttf)

    set(atlas_png ${CMAKE_SOURCE_DIR}/fonts/${atlas_name}.png)
    set(atlas_json ${CMAKE_SOURCE_DIR}/fonts/${atlas_name}.json)

    add_custom_command(
        OUTPUT ${atlas_png} ${atlas_json}
        COMMAND $<TARGET_FILE:generate_sdf_atlas>
            --font ${CMAKE_SOURCE_DIR}/fonts/${atlas_ttf}
            --output ${atlas_png}
            --metadata ${atlas_json}
        DEPENDS generate_sdf_atlas ${CMAKE_SOURCE_DIR}/fonts/${atlas_ttf}
        COMMENT "Generating SDF font atlas (fonts/${atlas_name}.png)"
        VERBATIM
    )

    list(APPEND SDF_ATLAS_OUTPUTS ${atlas_png} ${atlas_json})
endforeach()

add_custom_target(font-atlas DEPENDS ${SDF_ATLAS_OUTPUTS})
add_dependencies(world-sim font-atlas)
add_dependencies(ui-sandbox font-atlas)
add_dependencies(asset-manager font-atlas)

# Quickstart planet — the prebuilt planet that ships with the game. Players never
# generate it; the main menu disables Quick Start when it is absent. This is an
# EXPLICIT target (deliberately NOT part of ALL): generating the n=2048 planet
# takes minutes, so it is not imposed on routine builds. Run it once (CI and
# packaging do too) with an optimized config:
#   cmake --build build --config RelWithDebInfo --target quickstart-planet
# It generates the planet (only if missing) and stages it next to the world-sim
# exe at assets/planets/, where findResource locates it. The seed/n here are the
# single source of truth for the shipped planet.
set(QUICKSTART_PLANET_SRC ${CMAKE_SOURCE_DIR}/planets/quickstart.wsplanet)

# Generate the source planet once (OUTPUT-gated: only runs if the file is absent).
add_custom_command(
    OUTPUT ${QUICKSTART_PLANET_SRC}
    COMMAND $<TARGET_FILE:worldgen-cli>
        --n 2048 --seed 424242 --save-planet ${QUICKSTART_PLANET_SRC}
    DEPENDS worldgen-cli
    COMMENT "Generating quickstart planet (n=2048, seed 424242) — one-time, several minutes"
    VERBATIM
)

# Stage it next to the world-sim exe. The genex path is per-config, so it lands
# beside whichever config the target is built in. copy_if_different skips when
# unchanged, so re-running the target is cheap.
add_custom_target(quickstart-planet
    DEPENDS ${QUICKSTART_PLANET_SRC}
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:world-sim>/assets/planets
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${QUICKSTART_PLANET_SRC}
        $<TARGET_FILE_DIR:world-sim>/assets/planets/quickstart.wsplanet
    COMMENT "Staging quickstart planet next to world-sim"
    VERBATIM
)
add_dependencies(quickstart-planet world-sim)

# Developer client (web app)
# Multi-config generators (Visual Studio) have no CMAKE_BUILD_TYPE at configure
# time, so Windows/VS users opt in with -DBUILD_DEVELOPER_CLIENT=ON; single-config
# Debug/Development builds keep auto-building it.
option(BUILD_DEVELOPER_CLIENT "Build the React developer-client web app (requires npm)" OFF)
if(BUILD_DEVELOPER_CLIENT OR CMAKE_BUILD_TYPE STREQUAL "Development" OR CMAKE_BUILD_TYPE STREQUAL "Debug")
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

# world-sim's POST_BUILD step copies the shared build/shaders directory but
# does not link planet-view, so parallel MSBuild can schedule planet-view's
# shader staging concurrently with that copy (Windows file locks fail it).
if(TARGET world-sim AND TARGET planet-view-shaders)
    add_dependencies(world-sim planet-view-shaders)
endif()
