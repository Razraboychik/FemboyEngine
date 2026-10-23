#!/usr/bin/env bash
# Builds Neon Triangle on macOS. Run from the repository root.
#
# Requires the Xcode command line tools and CMake 3.20+ (brew install cmake
# ninja). SFML is fetched and built by CMake by default.
set -euo pipefail
cd "$(dirname "$0")"

BUILD_DIR="${BUILD_DIR:-build-macos}"
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
