# Nitty Plugin Documentation Index

Welcome to the Nitty plugin documentation hub. Here you'll find everything needed
to build, test, and distribute Nitty plugins.

---

## Documentation

| Document | Description |
|---|---|
| [Plugin Development Guide](../plugin-development-guide.md) | Full guide: architecture, quick start, all extension points, testing, publishing |
| [Plugin API Reference](../plugin-api-reference.md) | Every `api_*` function with signature, parameters, and examples |
| [Plugin Manifest Specification](../plugin-manifest.md) | `plugin.yaml` format, all fields, compatibility rules |

---

## Examples

| Example | Extension Points | Description |
|---|---|---|
| [word-highlighter](../../examples/plugins/word-highlighter/) | TerminalDecorator, SettingsPanel, ContextMenu, EventBus | Highlights keywords in terminal output with ANSI colors |
| [template](../../examples/plugins/template/) | All (commented out) | Minimal starter template for new plugins |

---

## Scaffolding

Create a new plugin from the template:

```sh
# From the Nitty repository root:
./scripts/create-plugin.sh my-plugin-name
```

---

## Extension Points — Quick Reference

| Extension Point | Registration Function | Max | Purpose |
|---|---|---|---|
| `TerminalDecorator` | `api_register_terminal_decorator(api, callback_id)` | 16 | Transform/annotate terminal output |
| `HotkeyProvider` | `api_register_hotkey(api, action, chord, desc, callback_id)` | 64 | Add keyboard shortcuts |
| `ToolbarButton` | `api_register_toolbar_button(api, id, icon, tooltip, pos, callback_id)` | 32 | Add toolbar buttons |
| `TabHeader` | `api_register_tab_header(api, id, position, callback_id)` | 16 | Add tab header widgets |
| `ContextMenuProvider` | `api_register_context_menu_item(api, id, label, icon, group, callback_id)` | 32 | Add right-click menu items |
| `ConnectionProvider` | `api_register_connection_provider(api, type, display, icon, callback_id)` | 8* | Register new connection types |
| `SettingsPanel` | `api_register_setting(api, plugin_name, key, label, type, default)` | 64 | Define configurable settings |

> *3 of 8 ConnectionProvider slots are used by built-in providers (SSH, Serial, Telnet).

---

## Built-in Events

| Event | When it fires |
|---|---|
| `"nitty.tab.created"` | A new tab opens |
| `"nitty.tab.closed"` | A tab closes |
| `"nitty.tab.activated"` | Active tab changes |
| `"nitty.pane.created"` | A pane split is created |
| `"nitty.pane.closed"` | A pane closes |
| `"nitty.pane.focused"` | Focus moves to a pane |
| `"nitty.connection.opened"` | A connection is established |
| `"nitty.connection.closed"` | A connection drops |
| `"nitty.config.changed"` | Config file is reloaded |

Subscribe: `api_subscribe(api, "nitty.config.changed", callback_id)`  
Emit: `api_emit(api, "my-plugin.event", data)`

---

## Plugin Discovery Directories

Nitty scans for plugins in this order (first match wins):

1. `~/.config/nitty/plugins/` — user plugins (highest priority)
2. `/usr/share/nitty/plugins/` — system plugins
3. `./plugins/` — development (CWD-relative)

Each directory is scanned for subdirectories containing a `plugin.yaml`.

---

## API Version Compatibility

| Plugin `api_version` | Runs on Nitty 0.10? |
|---|---|
| `"0.9"` | ✅ (minor ≤ current) |
| `"0.10"` | ✅ (exact match) |
| `"0.11"` | ❌ (future minor) |
| `"1.0"` | ❌ (major mismatch) |

Rule: **major must match exactly; plugin minor must be ≤ Nitty minor**.

---

## FAQ

**Q: My plugin isn't showing up in the Plugin Manager.**  
A: Check that `plugin.yaml` is valid and in the correct directory. Look for
errors in Nitty's stdout output at startup.

**Q: Registration returns -1. What does that mean?**  
A: The extension point registry is full, or you have a duplicate ID/action.
Check capacity limits in the table above and ensure IDs are globally unique.

**Q: My `plugin_destroy()` — do I need to manually unsubscribe from events?**  
A: No. `event_bus_remove_plugin(slot)` is called automatically after
`plugin_destroy()`. You only need to unregister extension points
(`api_unregister_hotkey`, etc.) manually.

**Q: How do I persist plugin settings?**  
A: In v0.10, `api_get_config` is a stub. Parse your own TOML/YAML file
with `nitpick-toml` or `nitpick-yaml`. Config persistence is planned for v0.11.

**Q: Can I store function pointers in a global array?**  
A: No — Nitpick does not support storing function pointers in arrays. Use the
callback_id pattern: store an integer ID, and dispatch by matching IDs in a
`if/else` chain in your event handler function.

**Q: The test utilities `mock_log_count` always returns 0. How do I test logging?**  
A: In v0.10, `api_log` writes directly to stdout with no capture. For log
verification, redirect stdout in your test runner and scan the output for
expected strings.

---

## Plugin API Changelog

| Version | Changes |
|---|---|
| v0.10.0 | Plugin system architecture: discovery, manifest, loader, plugin_manager |
| v0.10.1 | `PluginAPI` struct, all `api_*` accessor functions, all `ext_*.npk` modules |
| v0.10.2 | Event bus (`api_subscribe`, `api_emit`), data/cache dir stubs, `ext_config.npk` |
| v0.10.3 | `plugin_manager_enable`, `plugin_manager_disable`, dependency resolution |
| v0.10.4 | Plugin Manager UI (Ctrl+Shift+P), error notifications on startup failures |
| v0.10.5 | Plugin Development Kit: guide, API reference, examples, test utilities, scaffolding |
