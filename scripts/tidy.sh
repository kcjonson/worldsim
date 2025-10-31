#!/bin/bash
#
# Tidy Script - Run clang-tidy on C++ source files
#
# Usage:
#   ./scripts/tidy              # Check all files
#   ./scripts/tidy --changed    # Check only files changed vs main branch
#   ./scripts/tidy --staged     # Check only staged files
#

set -e

# Determine clang-tidy command based on OS
if command -v clang-tidy &> /dev/null; then
  CLANG_TIDY="clang-tidy"
elif [[ "$OSTYPE" == "darwin"* ]]; then
  # Check Homebrew paths (both Apple Silicon and Intel)
  if [ -f "/opt/homebrew/opt/llvm/bin/clang-tidy" ]; then
    CLANG_TIDY="/opt/homebrew/opt/llvm/bin/clang-tidy"
  elif [ -f "/usr/local/opt/llvm/bin/clang-tidy" ]; then
    CLANG_TIDY="/usr/local/opt/llvm/bin/clang-tidy"
  else
    echo "❌ clang-tidy not found"
    echo "Install with: brew install llvm"
    exit 1
  fi
else
  echo "❌ clang-tidy not found"
  echo "Install with: apt-get install clang-tidy (Linux)"
  exit 1
fi

# Check for build directory with compile_commands.json
if [ ! -f "build/compile_commands.json" ]; then
  echo "❌ build/compile_commands.json not found"
  echo "Run 'cmake -B build' with CMAKE_EXPORT_COMPILE_COMMANDS=ON"
  exit 1
fi

# Parse arguments
MODE="all"

while [[ $# -gt 0 ]]; do
  case $1 in
    --changed)
      MODE="changed"
      shift
      ;;
    --staged)
      MODE="staged"
      shift
      ;;
    --all)
      MODE="all"
      shift
      ;;
    *)
      echo "Unknown option: $1"
      echo "Usage: $0 [--all|--changed|--staged]"
      exit 1
      ;;
  esac
done

# Get files to check
FILES=""

if [ "$MODE" = "staged" ]; then
  # Get staged .cpp files only (clang-tidy needs implementation files)
  FILES=$(git diff --cached --name-only --diff-filter=ACM | grep -E '\.cpp$' || true)

  if [ -z "$FILES" ]; then
    echo "No C++ implementation files staged, skipping clang-tidy"
    exit 0
  fi

  echo "Checking staged files:"
elif [ "$MODE" = "changed" ]; then
  # Get changed .cpp files vs main branch
  FILES=$(git diff --name-only origin/main...HEAD | grep -E '\.cpp$' || true)

  if [ -z "$FILES" ]; then
    echo "No C++ implementation files changed vs main, skipping clang-tidy"
    exit 0
  fi

  echo "Checking changed files:"
else
  # Get all .cpp files
  FILES=$(find libs apps -name '*.cpp' | tr '\n' ' ')
  echo "Checking all files:"
fi

echo "$FILES"
echo ""

# Run clang-tidy
echo "Running clang-tidy..."
echo "$FILES" | xargs $CLANG_TIDY -p build

EXIT_CODE=$?

if [ $EXIT_CODE -ne 0 ]; then
  echo ""
  echo "❌ clang-tidy found issues"
  echo "Fix the errors above or update .clang-tidy to disable specific checks"
  exit 1
fi

echo ""
echo "✅ clang-tidy check passed"
exit 0
