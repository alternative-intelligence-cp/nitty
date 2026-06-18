# Nitty Plugin Template

A minimal starter template for building Nitty plugins.

## Quick Start

**Option A: Use the scaffolding script (recommended)**

```sh
# From the Nitty repo root:
./scripts/create-plugin.sh my-plugin-name
```

**Option B: Manual setup**

```sh
# Copy template to your plugins directory
cp -r examples/plugins/template ~/.config/nitty/plugins/my-plugin-name

# Edit the manifest
sed -i "s/PLACEHOLDER_NAME/my-plugin-name/g" \
    ~/.config/nitty/plugins/my-plugin-name/plugin.yaml

# Edit author and description in plugin.yaml

# Edit main.npk — replace PLACEHOLDER_NAME with your plugin name
# Uncomment the extension points you want to use

# Compile
cd ~/.config/nitty/plugins/my-plugin-name
nitpickc main.npk -o main

# Restart Nitty
```

## Files

| File | Purpose |
|---|---|
| `plugin.yaml` | Plugin manifest — metadata and extension point declarations |
| `main.npk` | Plugin source — `plugin_init`, `plugin_destroy`, callbacks |

## Extension Points

Uncomment the relevant sections in `main.npk`:

| Extension Point | What it does |
|---|---|
| `TerminalDecorator` | Transform terminal output (highlight, annotate) |
| `HotkeyProvider` | Add keyboard shortcuts |
| `ToolbarButton` | Add buttons to the main toolbar |
| `TabHeader` | Add badges/indicators to tab headers |
| `ContextMenuProvider` | Add right-click menu items |
| `ConnectionProvider` | Register a new connection type |
| `SettingsPanel` | Define configurable settings |

## Next Steps

- Read the [Plugin Development Guide](../../../docs/plugin-development-guide.md)
- See the [API Reference](../../../docs/plugin-api-reference.md)
- Browse the [Word Highlighter example](../word-highlighter/) for a full example

## Tips

- Always check registration return values (< 0 means failure)
- Use `api_log(api, "info"|"warn"|"error", message)` for structured logging
- Implement `plugin_destroy()` carefully — unregister everything you registered
- Test headlessly using `src/plugin/test_utils.npk`
