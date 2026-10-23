#!/usr/bin/env bash
# Check or install the tools needed to build FemboyEngine on Debian/Ubuntu.
set -euo pipefail

MODE="${1:---check}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PACKAGES=(
    build-essential
    cmake
    ninja-build
    git
    pkg-config
    libx11-dev
    libxrandr-dev
    libxcursor-dev
    libxi-dev
    libudev-dev
    libgl1-mesa-dev
    libfreetype-dev
    libopenal-dev
    libvorbis-dev
    libflac-dev
)

missing=()
for tool in cmake ninja git c++; do
    if command -v "$tool" >/dev/null 2>&1; then
        echo "[ok]   $tool -> $(command -v "$tool")"
    else
        echo "[miss] $tool"
        missing+=("$tool")
    fi
done

if [[ "${#missing[@]}" -gt 0 ]]; then
    if [[ "$MODE" == "--install" ]]; then
        if ! command -v apt-get >/dev/null 2>&1; then
            echo "Automatic install is implemented for Debian/Ubuntu (apt-get)." >&2
            echo "Install: ${PACKAGES[*]}" >&2
            exit 1
        fi
        sudo apt-get update
        sudo apt-get install -y --no-install-recommends "${PACKAGES[@]}"
    else
        echo
        echo "Missing: ${missing[*]}"
        echo "Install with:"
        echo "  ./tools/setup_linux.sh --install"
        echo
        echo "Packages: ${PACKAGES[*]}"
        exit 1
    fi
fi

echo
echo "Linux toolchain check complete."
echo "Configure and build:"
echo "  ./build-linux.sh"
