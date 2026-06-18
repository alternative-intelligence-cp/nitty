#!/usr/bin/env bash
# scripts/create-plugin.sh — Nitty plugin scaffolding script
#
# Usage: ./scripts/create-plugin.sh <plugin-name>
#
# Creates a new plugin directory at ~/.config/nitty/plugins/<name>/
# by copying the template and substituting placeholder values.
#
# Example:
#   ./scripts/create-plugin.sh my-cool-plugin

set -e

# ── Argument validation ───────────────────────────────────────────────────────

if [ -z "$1" ]; then
    echo "Usage: $0 <plugin-name>"
    echo ""
    echo "  <plugin-name>  Plugin identifier in kebab-case (e.g., my-plugin)"
    echo ""
    echo "Example:"
    echo "  ./scripts/create-plugin.sh word-count"
    exit 1
fi

NAME="$1"

# Validate name: kebab-case, no spaces, no uppercase
if ! echo "$NAME" | grep -qE '^[a-z][a-z0-9-]*$'; then
    echo "Error: Plugin name must be kebab-case (lowercase letters, digits, hyphens)."
    echo "  Valid:   my-plugin, word-count, ssh-helper"
    echo "  Invalid: MyPlugin, my plugin, my_plugin"
    exit 1
fi

# ── Locate script and repo root ───────────────────────────────────────────────

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
TEMPLATE_DIR="$REPO_ROOT/examples/plugins/template"

if [ ! -d "$TEMPLATE_DIR" ]; then
    echo "Error: Template directory not found at $TEMPLATE_DIR"
    echo "Run this script from the Nitty repository root."
    exit 1
fi

# ── Target directory ──────────────────────────────────────────────────────────

PLUGINS_DIR="${NITTY_PLUGINS_DIR:-$HOME/.config/nitty/plugins}"
DEST="$PLUGINS_DIR/$NAME"

if [ -d "$DEST" ]; then
    echo "Error: Plugin directory already exists: $DEST"
    echo "Choose a different name or remove the existing directory first."
    exit 1
fi

# ── Create plugin ─────────────────────────────────────────────────────────────

echo "Creating plugin '$NAME' at $DEST ..."

# Copy template
mkdir -p "$DEST"
cp "$TEMPLATE_DIR/plugin.yaml" "$DEST/plugin.yaml"
cp "$TEMPLATE_DIR/main.npk"   "$DEST/main.npk"
cp "$TEMPLATE_DIR/README.md"  "$DEST/README.md"

# Substitute PLACEHOLDER_NAME in all copied files
sed -i "s/PLACEHOLDER_NAME/$NAME/g" "$DEST/plugin.yaml"
sed -i "s/PLACEHOLDER_NAME/$NAME/g" "$DEST/main.npk"
sed -i "s/PLACEHOLDER_NAME/$NAME/g" "$DEST/README.md"

# ── Done ──────────────────────────────────────────────────────────────────────

echo ""
echo "Plugin '$NAME' created successfully!"
echo ""
echo "Next steps:"
echo "  1. Edit $DEST/plugin.yaml"
echo "     - Set author and description"
echo "  2. Edit $DEST/main.npk"
echo "     - Uncomment the extension points you need"
echo "     - Implement your plugin logic"
echo "  3. Compile:"
echo "     cd $DEST && nitpickc main.npk -o main"
echo "  4. Restart Nitty"
echo "     Your plugin will appear in the Plugin Manager (Ctrl+Shift+P)"
echo ""
echo "Documentation: $REPO_ROOT/docs/plugin-development-guide.md"
