#!/bin/bash
set -e

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${REPO_DIR}/build-deb"

echo "Building Nitty release binary..."
cd "$REPO_DIR"
npkbld build --release

echo "Preparing Debian package staging directory..."
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR/nitty_0.14.0-1_amd64/DEBIAN"
mkdir -p "$BUILD_DIR/nitty_0.14.0-1_amd64/usr/bin"
mkdir -p "$BUILD_DIR/nitty_0.14.0-1_amd64/usr/share/applications"
mkdir -p "$BUILD_DIR/nitty_0.14.0-1_amd64/usr/share/metainfo"
mkdir -p "$BUILD_DIR/nitty_0.14.0-1_amd64/usr/share/man/man1"

# Copy binary
cp .nitpick_make/build/nitty "$BUILD_DIR/nitty_0.14.0-1_amd64/usr/bin/"

# Copy desktop and metainfo
cp packaging/nitty.desktop "$BUILD_DIR/nitty_0.14.0-1_amd64/usr/share/applications/"
cp packaging/nitty.metainfo.xml "$BUILD_DIR/nitty_0.14.0-1_amd64/usr/share/metainfo/"

# Copy man page
cp docs/nitty.1 "$BUILD_DIR/nitty_0.14.0-1_amd64/usr/share/man/man1/"
gzip -9 "$BUILD_DIR/nitty_0.14.0-1_amd64/usr/share/man/man1/nitty.1"

# Copy icons
for size in 16 24 32 48 64 128 256 512; do
  mkdir -p "$BUILD_DIR/nitty_0.14.0-1_amd64/usr/share/icons/hicolor/${size}x${size}/apps"
  cp "packaging/icons/nitty-${size}x${size}.png" "$BUILD_DIR/nitty_0.14.0-1_amd64/usr/share/icons/hicolor/${size}x${size}/apps/nitty.png"
done

# Set permissions
chmod 755 "$BUILD_DIR/nitty_0.14.0-1_amd64/usr/bin/nitty"
find "$BUILD_DIR/nitty_0.14.0-1_amd64/usr/share" -type f -exec chmod 644 {} \;

# Copy Debian control files
cp packaging/deb/control "$BUILD_DIR/nitty_0.14.0-1_amd64/DEBIAN/"
cp packaging/deb/postinst "$BUILD_DIR/nitty_0.14.0-1_amd64/DEBIAN/"
cp packaging/deb/postrm "$BUILD_DIR/nitty_0.14.0-1_amd64/DEBIAN/"
chmod 755 "$BUILD_DIR/nitty_0.14.0-1_amd64/DEBIAN/postinst"
chmod 755 "$BUILD_DIR/nitty_0.14.0-1_amd64/DEBIAN/postrm"

# Build package
echo "Building .deb package..."
cd "$BUILD_DIR"
dpkg-deb --build "nitty_0.14.0-1_amd64"

echo "Package built: $BUILD_DIR/nitty_0.14.0-1_amd64.deb"

# Optional: verify with lintian
if command -v lintian >/dev/null 2>&1; then
    lintian "nitty_0.14.0-1_amd64.deb" || true
fi
