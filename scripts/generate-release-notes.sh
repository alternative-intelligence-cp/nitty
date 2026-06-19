#!/bin/bash
# Extracts release notes for a specific version from CHANGELOG.md

if [ -z "$1" ]; then
    echo "Usage: $0 <version-tag> (e.g. v0.14.0)"
    exit 1
fi

# Strip leading 'v' if present
VERSION="${1#v}"

# Find the start line for the requested version
START_LINE=$(awk "/^## \[${VERSION}\]/ {print NR}" CHANGELOG.md | head -n 1)

if [ -z "$START_LINE" ]; then
    echo "Version $VERSION not found in CHANGELOG.md."
    exit 1
fi

# Find the start line for the NEXT version to know where to stop
END_LINE=$(awk "NR > ${START_LINE} && /^## \[/ {print NR}" CHANGELOG.md | head -n 1)

echo "## Nitty Release $1"
echo ""

# Extract the relevant lines
if [ -z "$END_LINE" ]; then
    # This is the last version in the file
    tail -n +$((START_LINE + 1)) CHANGELOG.md
else
    # Output lines between START_LINE and END_LINE
    head -n $((END_LINE - 1)) CHANGELOG.md | tail -n +$((START_LINE + 1))
fi

echo ""
echo "### Installation"
echo 'Download the `.deb`, `.rpm`, `.AppImage`, or `.flatpak` package from the assets below.'
