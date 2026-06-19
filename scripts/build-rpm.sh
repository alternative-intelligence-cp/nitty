#!/bin/bash
set -e

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${REPO_DIR}/build-rpm"

echo "Setting up rpmbuild environment..."
mkdir -p "$BUILD_DIR"/{BUILD,RPMS,SOURCES,SPECS,SRPMS}

# Create source tarball
VERSION="0.14.0"
echo "Creating source tarball nitty-${VERSION}.tar.gz..."
cd "$REPO_DIR"
git archive --format=tar.gz --prefix="nitty-${VERSION}/" HEAD > "$BUILD_DIR/SOURCES/nitty-${VERSION}.tar.gz"

# Copy spec file
cp packaging/rpm/nitty.spec "$BUILD_DIR/SPECS/"

# Build RPM
echo "Building RPM package..."
rpmbuild --define "_topdir $BUILD_DIR" -ba "$BUILD_DIR/SPECS/nitty.spec"

echo "RPM built successfully in $BUILD_DIR/RPMS/x86_64/"

# Optional: verify with rpmlint
if command -v rpmlint >/dev/null 2>&1; then
    rpmlint "$BUILD_DIR/RPMS/x86_64/"*.rpm || true
fi
