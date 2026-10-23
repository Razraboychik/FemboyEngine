#!/usr/bin/env bash
# Check or install the tools needed to build FemboyEngine on macOS.
set -euo pipefail

MODE="${1:---check}"

missing=()
if xcode-select -p >/dev/null 2>&1; then
    echo "[ok]   Xcode Command Line Tools"
else
    echo "[miss] Xcode Command Line Tools"
    missing+=("xcode")
fi

for tool in cmake ninja git; do
    if command -v "$tool" >/dev/null 2>&1; then
        echo "[ok]   $tool -> $(command -v "$tool")"
    else
        echo "[miss] $tool"
        missing+=("$tool")
    fi
done

if [[ "${#missing[@]}" -gt 0 ]]; then
    if [[ "$MODE" == "--install" ]]; then
        if [[ " ${missing[*]} " == *" xcode "* ]]; then
            echo "Opening the Xcode Command Line Tools installer..."
            xcode-select --install || true
            echo "Finish that installer, then rerun this script."
        fi
        if ! command -v brew >/dev/null 2>&1; then
            echo "Homebrew is required for cmake/ninja/git. Install it from https://brew.sh/" >&2
            exit 1
        fi
        brew install cmake ninja git
    else
        echo
        echo "Missing: ${missing[*]}"
        echo "Install with:"
        echo "  ./tools/setup_macos.sh --install"
        exit 1
    fi
fi

echo
echo "macOS toolchain check complete."
echo "Configure and build:"
echo "  ./build-macos.sh"
