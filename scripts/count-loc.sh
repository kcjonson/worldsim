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

# Helper function to count lines in files
count_lines() {
    local file_list="$1"
    if [ -z "$file_list" ]; then
        echo "0"
    else
        echo "$file_list" | xargs wc -l 2>/dev/null | tail -1 | awk '{print $1}'
    fi
}

# C++ Source Files
cpp_file_list=$(find . -type f \( -name "*.cpp" -o -name "*.h" -o -name "*.hpp" \) -not -path "./build/*" -not -path "./vcpkg_installed/*" 2>/dev/null)
cpp_files=$(echo "$cpp_file_list" | grep -c .)
cpp_lines=$(count_lines "$cpp_file_list")
echo -e "${GREEN}C++ Source Files (.cpp, .h, .hpp):${NC}"
echo "  Files: $cpp_files"
echo "  Lines: $cpp_lines"
echo ""

# CMake Files
cmake_file_list=$(find . -type f \( -name "CMakeLists.txt" -o -name "*.cmake" \) -not -path "./build/*" -not -path "./vcpkg_installed/*" 2>/dev/null)
cmake_files=$(echo "$cmake_file_list" | grep -c .)
cmake_lines=$(count_lines "$cmake_file_list")
echo -e "${GREEN}CMake Build Files:${NC}"
echo "  Files: $cmake_files"
echo "  Lines: $cmake_lines"
echo ""

# Shader Files
shader_file_list=$(find . -type f \( -name "*.glsl" -o -name "*.vert" -o -name "*.frag" -o -name "*.geom" \) -not -path "./build/*" -not -path "./vcpkg_installed/*" 2>/dev/null)
shader_files=$(echo "$shader_file_list" | grep -c .)
shader_lines=$(count_lines "$shader_file_list")
echo -e "${GREEN}Shader Files (.glsl, .vert, .frag, .geom):${NC}"
echo "  Files: $shader_files"
echo "  Lines: $shader_lines"
echo ""

# Script Files
script_file_list=$(find . -type f \( -name "*.sh" -o -name "*.py" \) -not -path "./build/*" -not -path "./vcpkg_installed/*" 2>/dev/null)
script_files=$(echo "$script_file_list" | grep -c .)
script_lines=$(count_lines "$script_file_list")
echo -e "${GREEN}Script Files (.sh, .py):${NC}"
echo "  Files: $script_files"
echo "  Lines: $script_lines"
echo ""

# Documentation
doc_file_list=$(find . -type f -name "*.md" -not -path "./build/*" -not -path "./vcpkg_installed/*" 2>/dev/null)
doc_files=$(echo "$doc_file_list" | grep -c .)
doc_lines=$(count_lines "$doc_file_list")
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
