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
find_package(glew CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)

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

# Enable testing
enable_testing()
