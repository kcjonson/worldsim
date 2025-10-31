#!/bin/bash
#
# Lint Script - Run clang-format on C++ source files
#
# Usage:
#   ./scripts/lint              # Check all files (dry-run)
#   ./scripts/lint --fix        # Format all files
#   ./scripts/lint --staged     # Format only staged files (for pre-commit hook)
#   ./scripts/lint --check      # Check all files (exit 1 on formatting issues)
#

set -e

# Determine clang-format command based on OS
if [[ "$OSTYPE" == "darwin"* ]]; then
  # macOS - use xcrun
  CLANG_FORMAT="xcrun clang-format"
else
  # Linux/other - use system clang-format
  CLANG_FORMAT="clang-format"
fi

# Parse arguments
MODE="check"
FILES=""

while [[ $# -gt 0 ]]; do
  case $1 in
    --fix)
      MODE="fix"
      shift
      ;;
    --check)
      MODE="check"
      shift
      ;;
    --staged)
      MODE="staged"
      shift
      ;;
    *)
      echo "Unknown option: $1"
      echo "Usage: $0 [--fix|--check|--staged]"
      exit 1
      ;;
  esac
done

# Get files to format
if [ "$MODE" = "staged" ]; then
  # Get staged files only
  FILES=$(git diff --cached --name-only --diff-filter=ACM | grep -E '\.(cpp|h)$' || true)

  if [ -z "$FILES" ]; then
    echo "No C++ files staged, skipping format check"
    exit 0
  fi
else
  # Get all files
  FILES=$(find libs apps \( -name '*.cpp' -o -name '*.h' \) | tr '\n' ' ')
fi

# Run clang-format
if [ "$MODE" = "fix" ] || [ "$MODE" = "staged" ]; then
  # Format files in-place
  echo "Formatting files..."
  for FILE in $FILES; do
    if [ -f "$FILE" ]; then
      echo "  Formatting: $FILE"
      $CLANG_FORMAT -i "$FILE"

      # Re-stage file if in staged mode
      if [ "$MODE" = "staged" ]; then
        git add "$FILE"
      fi
    fi
  done

  if [ "$MODE" = "staged" ]; then
    echo "Files formatted and re-staged"
  else
    echo "✅ Files formatted"
  fi
else
  # Check mode - dry run with Werror
  echo "Checking code formatting..."
  echo "$FILES" | xargs $CLANG_FORMAT --dry-run --Werror
  EXIT_CODE=$?

  if [ $EXIT_CODE -ne 0 ]; then
    echo ""
    echo "❌ Code formatting check failed."
    echo "Run './scripts/lint --fix' to format files."
    exit 1
  fi

  echo "✅ Code formatting check passed"
fi

exit 0
