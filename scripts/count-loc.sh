#!/bin/bash
# Count lines of code in the world-sim repository
# Usage: ./scripts/count-loc.sh

set -e

# Colors for output
BLUE='\033[0;34m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${BLUE}=== World-Sim Lines of Code Report ===${NC}"
echo ""

# C++ Source Files
cpp_files=$(find . -type f \( -name "*.cpp" -o -name "*.h" -o -name "*.hpp" \) -not -path "./build/*" -not -path "./vcpkg_installed/*" | wc -l)
cpp_lines=$(find . -type f \( -name "*.cpp" -o -name "*.h" -o -name "*.hpp" \) -not -path "./build/*" -not -path "./vcpkg_installed/*" -exec wc -l {} + 2>/dev/null | tail -1 | awk '{print $1}')
echo -e "${GREEN}C++ Source Files (.cpp, .h, .hpp):${NC}"
echo "  Files: $cpp_files"
echo "  Lines: $cpp_lines"
echo ""

# CMake Files
cmake_files=$(find . -type f \( -name "CMakeLists.txt" -o -name "*.cmake" \) -not -path "./build/*" -not -path "./vcpkg_installed/*" | wc -l)
cmake_lines=$(find . -type f \( -name "CMakeLists.txt" -o -name "*.cmake" \) -not -path "./build/*" -not -path "./vcpkg_installed/*" -exec wc -l {} + 2>/dev/null | tail -1 | awk '{print $1}')
echo -e "${GREEN}CMake Build Files:${NC}"
echo "  Files: $cmake_files"
echo "  Lines: $cmake_lines"
echo ""

# Shader Files
shader_files=$(find . -type f \( -name "*.glsl" -o -name "*.vert" -o -name "*.frag" -o -name "*.geom" \) -not -path "./build/*" 2>/dev/null | wc -l)
if [ "$shader_files" -gt 0 ]; then
    shader_lines=$(find . -type f \( -name "*.glsl" -o -name "*.vert" -o -name "*.frag" -o -name "*.geom" \) -not -path "./build/*" -exec wc -l {} + 2>/dev/null | tail -1 | awk '{print $1}')
else
    shader_lines=0
fi
echo -e "${GREEN}Shader Files (.glsl, .vert, .frag, .geom):${NC}"
echo "  Files: $shader_files"
echo "  Lines: $shader_lines"
echo ""

# Script Files
script_files=$(find . -type f \( -name "*.sh" -o -name "*.py" \) -not -path "./build/*" -not -path "./vcpkg_installed/*" 2>/dev/null | wc -l)
if [ "$script_files" -gt 0 ]; then
    script_lines=$(find . -type f \( -name "*.sh" -o -name "*.py" \) -not -path "./build/*" -not -path "./vcpkg_installed/*" -exec wc -l {} + 2>/dev/null | tail -1 | awk '{print $1}')
else
    script_lines=0
fi
echo -e "${GREEN}Script Files (.sh, .py):${NC}"
echo "  Files: $script_files"
echo "  Lines: $script_lines"
echo ""

# Documentation
doc_files=$(find . -type f -name "*.md" -not -path "./build/*" -not -path "./vcpkg_installed/*" 2>/dev/null | wc -l)
if [ "$doc_files" -gt 0 ]; then
    doc_lines=$(find . -type f -name "*.md" -not -path "./build/*" -not -path "./vcpkg_installed/*" -exec wc -l {} + 2>/dev/null | tail -1 | awk '{print $1}')
else
    doc_lines=0
fi
echo -e "${GREEN}Documentation (.md):${NC}"
echo "  Files: $doc_files"
echo "  Lines: $doc_lines"
echo ""

# Total Source Code (excluding docs)
total_code_lines=$((cpp_lines + cmake_lines + shader_lines + script_lines))
echo -e "${YELLOW}=== TOTAL SOURCE CODE (excluding documentation) ===${NC}"
echo "  Lines: $total_code_lines"
echo ""

# Grand Total
grand_total=$((total_code_lines + doc_lines))
echo -e "${YELLOW}=== GRAND TOTAL (including documentation) ===${NC}"
echo "  Lines: $grand_total"
echo ""
