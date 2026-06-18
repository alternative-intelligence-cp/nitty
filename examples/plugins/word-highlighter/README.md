# Word Highlighter Plugin

A Nitty terminal plugin that highlights configured keywords in terminal output
with ANSI color codes.

## Features

- Highlights keywords in real time as terminal output arrives
- Configurable keyword list (comma-separated)
- Enable/disable via Plugin Manager (Ctrl+Shift+P)
- Right-click → "Add Word to Highlights" context menu item
- Reloads keywords when Nitty config changes

## Installation

```sh
# Copy plugin to your Nitty plugins directory
cp -r word-highlighter ~/.config/nitty/plugins/

# Compile the plugin
cd ~/.config/nitty/plugins/word-highlighter
nitpickc main.npk -o main

# Restart Nitty — the plugin will be loaded automatically
```

## Configuration

Open the Plugin Manager (Ctrl+Shift+P), select **word-highlighter**, and
click **Settings** to configure:

| Setting | Default | Description |
|---|---|---|
| `word-highlighter.keywords` | `error,warn,todo,fixme,bug` | Comma-separated keywords to highlight |
| `word-highlighter.enabled` | `true` | Enable or disable highlighting |
| `word-highlighter.color` | `1;33` | ANSI color code (bold yellow) |

> **Note:** Config persistence is coming in Nitty v0.11. In v0.10, the defaults
> are applied at startup. To change keywords, edit the `default_value` in
> `main.npk` and recompile.

## Highlight Colors

Change `word-highlighter.color` to any valid ANSI SGR code:

| Code | Color |
|---|---|
| `1;31` | Bold red |
| `1;32` | Bold green |
| `1;33` | Bold yellow (default) |
| `1;34` | Bold blue |
| `1;35` | Bold magenta |
| `1;36` | Bold cyan |
| `4;33` | Underline yellow |

## Extension Points Demonstrated

This plugin is an SDK example showing how to combine multiple extension points:

- **TerminalDecorator** — `api_register_terminal_decorator(api, WH_CB_DECORATOR)`
- **ContextMenuProvider** — `api_register_context_menu_item(api, ...)`
- **SettingsPanel** — `api_register_setting(api, "word-highlighter", ...)`
- **Event Bus** — `api_subscribe(api, "nitty.config.changed", ...)`

## Testing

Run the included test suite:

```sh
cd /path/to/nitty
nitpickc examples/plugins/word-highlighter/test_highlighter.npk \
  -L... -o /tmp/test_wh && /tmp/test_wh
```

See the [Plugin Development Guide](../../../docs/plugin-development-guide.md)
for detailed documentation.
