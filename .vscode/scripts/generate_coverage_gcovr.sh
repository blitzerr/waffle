#!/bin/sh
set -eu # Exit on error and on unset variables

# Expect workspaceFolder as the first argument
if [ -z "$1" ]; then
  echo "Error: Workspace folder argument is missing."
  exit 1
fi
WORKSPACE_ROOT="$1"

BUILD_COVERAGE_DIR="${WORKSPACE_ROOT}/build-coverage"
REPORT_OUTPUT_DIR_NAME="coverage_report_gcovr"
REPORT_OUTPUT_FULL_PATH="${BUILD_COVERAGE_DIR}/${REPORT_OUTPUT_DIR_NAME}"

# Create the report output directory
mkdir -p "${REPORT_OUTPUT_FULL_PATH}"

# Change to the build directory where .gcno and .gcda files are
cd "${BUILD_COVERAGE_DIR}" || exit 1 # Exit if cd fails

# Run gcovr
# Paths for --root and --filter are absolute.
# Paths for --html-details and --xml are relative to the current directory (BUILD_COVERAGE_DIR).
# The final '.' tells gcovr to search in the current directory.
gcovr \
  --verbose \
  --root "${WORKSPACE_ROOT}" \
  --filter "${WORKSPACE_ROOT}/src/waffle/" \
  --gcov-exclude ".*/examples/.*" \
  --gcov-executable "gcov" \
  --print-summary \
  --html-details "./${REPORT_OUTPUT_DIR_NAME}/index.html" \
  --xml "./coverage.xml" \
  . # Tell gcovr to make gcov run in the current directory (build-coverage)

echo "gcovr report generation complete."
echo "HTML report: ${REPORT_OUTPUT_FULL_PATH}/index.html"
echo "XML report: ${BUILD_COVERAGE_DIR}/coverage.xml"
