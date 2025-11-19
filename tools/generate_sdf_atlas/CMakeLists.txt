# SDF Atlas Generator Tool

cmake_minimum_required(VERSION 3.20)

project(generate_sdf_atlas)

# Find packages
find_package(msdfgen CONFIG REQUIRED)
find_package(Freetype REQUIRED)

# Create executable
add_executable(generate_sdf_atlas
    main.cpp
)

# Link libraries
target_link_libraries(generate_sdf_atlas PRIVATE
    msdfgen::msdfgen
    msdfgen::msdfgen-ext
    Freetype::Freetype
)

# C++20 standard
set_target_properties(generate_sdf_atlas PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

# Install to build/tools directory
install(TARGETS generate_sdf_atlas
    RUNTIME DESTINATION tools
)
