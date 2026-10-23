#!/usr/bin/env bash
# Builds Neon Triangle on Linux. Run from the repository root.
#
# SFML 3 is not packaged by most distributions yet, so the default is to let
# CMake download and build it (FEMBOY_FETCH_SFML=ON). Pass -DFEMBOY_SFML_ROOT
# to use a local build instead.
set -euo pipefail
cd "$(dirname "$0")"

BUILD_DIR="${BUILD_DIR:-build-linux}"
BUILD_TYPE="${BUILD_TYPE:-RelWithDebInfo}"

cmake -S . -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DFEMBOY_FETCH_SFML=ON \
    -DFEMBOY_BUILD_NEONTRIANGLE=ON \
    -DFEMBOY_BUILD_TESTS=ON

cmake --build "$BUILD_DIR"

if [[ "${SKIP_TESTS:-0}" != "1" ]]; then
    "./$BUILD_DIR/femboy_tests"
fi

echo "Build finished in $BUILD_DIR/"
