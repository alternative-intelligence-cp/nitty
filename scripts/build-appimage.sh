#!/bin/bash
set -e

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${REPO_DIR}/build-appimage"
APPDIR="${BUILD_DIR}/AppDir"

echo "Building Nitty release binary..."
cd "$REPO_DIR"
npkbld build --release

echo "Preparing AppDir structure..."
rm -rf "$BUILD_DIR"
mkdir -p "${APPDIR}/usr/bin"
mkdir -p "${APPDIR}/usr/lib"
mkdir -p "${APPDIR}/usr/share/applications"
mkdir -p "${APPDIR}/usr/share/metainfo"
mkdir -p "${APPDIR}/usr/share/icons/hicolor/256x256/apps"

# Copy binary
cp .nitpick_make/build/nitty "${APPDIR}/usr/bin/"

# Copy AppRun
cp packaging/appimage/AppRun "${APPDIR}/AppRun"
chmod +x "${APPDIR}/AppRun"

# Copy desktop file and icon to root (required by AppImage)
cp packaging/nitty.desktop "${APPDIR}/"
cp packaging/icons/nitty-256x256.png "${APPDIR}/nitty.png"
cp packaging/nitty.desktop "${APPDIR}/usr/share/applications/"
cp packaging/icons/nitty-256x256.png "${APPDIR}/usr/share/icons/hicolor/256x256/apps/nitty.png"

# Copy metainfo
cp packaging/nitty.metainfo.xml "${APPDIR}/usr/share/metainfo/"
# Sometimes linuxdeploy requires .appdata.xml
cp packaging/nitty.metainfo.xml "${APPDIR}/usr/share/metainfo/nitty.appdata.xml" || true

# Download linuxdeploy if not present
if [ ! -f "${BUILD_DIR}/linuxdeploy-x86_64.AppImage" ]; then
    echo "Downloading linuxdeploy..."
    wget -q https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage -O "${BUILD_DIR}/linuxdeploy-x86_64.AppImage"
    chmod +x "${BUILD_DIR}/linuxdeploy-x86_64.AppImage"
fi

# We skip building libssh2 into the AppDir here for simplicity in the script,
# but a production AppImage might bundle more libraries via linuxdeploy plugins.

echo "Running linuxdeploy to bundle dependencies and create AppImage..."
cd "$BUILD_DIR"
./linuxdeploy-x86_64.AppImage --appdir AppDir --output appimage

echo "AppImage built successfully in $BUILD_DIR/"
