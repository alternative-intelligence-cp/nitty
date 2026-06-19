#!/bin/bash
set -e

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${REPO_DIR}/build-flatpak"

echo "Setting up Flatpak build environment..."
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

if ! command -v flatpak-builder >/dev/null 2>&1; then
    echo "Error: flatpak-builder is not installed."
    exit 1
fi

echo "Building Flatpak bundle..."
flatpak-builder --force-clean build-dir "${REPO_DIR}/packaging/flatpak/com.nitty.Terminal.yml"

echo "Exporting Flatpak repository..."
flatpak-builder --repo=repo --force-clean build-dir "${REPO_DIR}/packaging/flatpak/com.nitty.Terminal.yml"

echo "Creating single-file .flatpak bundle..."
flatpak build-bundle repo com.nitty.Terminal.flatpak com.nitty.Terminal

echo "Flatpak built successfully: $BUILD_DIR/com.nitty.Terminal.flatpak"
echo "To install for testing: flatpak install --user com.nitty.Terminal.flatpak"
