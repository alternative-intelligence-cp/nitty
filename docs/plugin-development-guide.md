# Nitty Plugin Development Guide

**API version:** 0.10  
**Applies to:** Nitty v0.10.5+

---

## Table of Contents

1. [Introduction](#introduction)
2. [Architecture Overview](#architecture-overview)
3. [Quick Start](#quick-start)
4. [Plugin Manifest Reference](#plugin-manifest-reference)
5. [Extension Points](#extension-points)
   - [TerminalDecorator](#1-terminaldecorator)
   - [HotkeyProvider](#2-hotkeyprovider)
   - [ToolbarButton](#3-toolbarbutton)
   - [TabHeader](#4-tabheader)
   - [ContextMenuProvider](#5-contextmenuprovider)
   - [ConnectionProvider](#6-connectionprovider)
   - [SettingsPanel](#7-settingspanel)
6. [Event Bus](#event-bus)
7. [Plugin API Reference Summary](#plugin-api-reference-summary)
8. [Data & Cache Directories](#data--cache-directories)
9. [Logging](#logging)
10. [Configuration Access](#configuration-access)
11. [Error Handling](#error-handling)
12. [Testing Plugins](#testing-plugins)
13. [Publishing](#publishing)

---

## Introduction

Nitty plugins extend the terminal emulator with new capabilities without modifying
the core application. Plugins are written in **Nitpick** (the same language as
Nitty itself) and loaded dynamically at startup from a well-known directory on disk.

Each plugin declares what it provides via a `plugin.yaml` manifest and registers
its capabilities through a `PluginAPI` handle given to it during initialization.

**What plugins can do:**
- Decorate terminal output in real time (highlight keywords, transform ANSI sequences)
- Add keyboard shortcuts (hotkeys) mapped to plugin callbacks
- Add toolbar buttons to the main window
- Add tab header decorations (badges, indicators)
- Add context menu items on right-click
- Register new connection types (e.g., a custom serial protocol)
- Define settings in the built-in Settings dialog
- Subscribe to and emit Nitty events (tab opened, connection closed, config changed)

---

## Architecture Overview

```
~/.config/nitty/plugins/
    my-plugin/
        plugin.yaml      ← manifest
        main.npk         ← compiled Nitpick source

/usr/share/nitty/plugins/
    (system plugins)
```

**Startup sequence:**

```
1. discovery_scan_all()
       └─ finds every plugin.yaml in scan directories

2. manifest_init() / manifest_parse()
       └─ reads each plugin.yaml into memory

3. dependency_resolve()
       └─ topological sort; marks plugins with unmet deps as ERROR

4. loader_load_all()
       └─ validates api_version compatibility

5. plugin_manager_init()
       └─ for each valid plugin:
            a. ext_connection_init() + register_builtin_plugins()
            b. call plugin's plugin_init(api)  ← YOUR CODE RUNS HERE
            c. record ACTIVE state

6. At runtime: ext_*_dispatch_*()
       └─ invokes registered callbacks when events occur
```

---

## Quick Start

Create a minimal plugin in five steps:

**Step 1 — Create the directory:**
```sh
mkdir -p ~/.config/nitty/plugins/hello-world
```

**Step 2 — Write `plugin.yaml`:**
```yaml
name: hello-world
version: 0.1.0
api_version: "0.10"
author: "Your Name"
description: "My first Nitty plugin"
entry_module: main.npk
provides: []
```

**Step 3 — Write `main.npk`:**
```nitpick
use "../../src/plugin/api.npk".*;

pub func:plugin_init = NIL(PluginAPI:api) {
    drop(api_log(api, "info", "hello-world: loaded!"));
    pass(NIL);
};

pub func:plugin_destroy = NIL() {
    pass(NIL);
};

func:failsafe = int32(tbb32:_err) { exit 1i32; };
func:main     = int32() { exit 0i32; };
```

**Step 4 — Compile:**
```sh
cd ~/.config/nitty/plugins/hello-world
nitpickc main.npk -o main
```

**Step 5 — Restart Nitty.** Your plugin will appear in the Plugin Manager
(Ctrl+Shift+P) with status `[+] Active`.

---

## Plugin Manifest Reference

See [`docs/plugin-manifest.md`](plugin-manifest.md) for the full manifest
specification. Summary:

| Field | Required | Type | Example |
|---|---|---|---|
| `name` | ✅ | string | `"my-plugin"` |
| `version` | ✅ | string | `"1.0.0"` |
| `api_version` | ✅ | string | `"0.10"` |
| `entry_module` | ✅ | string | `"main.npk"` |
| `author` | — | string | `"Jane Doe"` |
| `description` | — | string | `"Does cool things"` |
| `dependencies` | — | list | see below |
| `provides` | — | list | `["TerminalDecorator"]` |

**`provides` tags** (informational — used by the Plugin Manager UI):
`TerminalDecorator`, `HotkeyProvider`, `ToolbarButton`, `TabHeader`,
`ContextMenuProvider`, `ConnectionProvider`, `SettingsPanel`

**`dependencies` example:**
```yaml
dependencies:
  - name: other-plugin
    version: ">=1.0.0"
```

---

## Extension Points

All registration functions are called from your `plugin_init(api)` function.
Unregistration is typically called from `plugin_destroy()`.

---

### 1. TerminalDecorator

**Purpose:** Intercept terminal data per-pane to transform or annotate output.

**When it fires:** When the terminal receives PTY output and `ext_terminal_dispatch_data`
is called. Currently, the dispatcher logs the count; full per-decorator callbacks
are wired in the dispatch pipeline (v0.10.2+).

**Registration:**
```nitpick
use "../../src/plugin/ext_terminal.npk".*;

pub func:plugin_init = NIL(PluginAPI:api) {
    // callback_id = your integer ID; dispatched via event bus
    int64:idx = raw api_register_terminal_decorator(api, 1i64);
    if (idx < 0i64) {
        drop(api_log(api, "error", "failed to register terminal decorator"));
    }
    pass(NIL);
};

pub func:plugin_destroy = NIL() {
    PluginAPI:api = raw api_make(MY_SLOT);
    drop(api_unregister_terminal_decorator(api));
    pass(NIL);
};
```

**Signature:**
```
api_register_terminal_decorator(api: PluginAPI, callback_id: int64) → int64
api_unregister_terminal_decorator(api: PluginAPI) → NIL
```

**Gotchas:**
- One decorator registration per plugin slot. A second call returns -1.
- `callback_id` is your dispatch identifier; subscribe to `EV_CONFIG_CHANGED`
  to reload keyword lists dynamically.

---

### 2. HotkeyProvider

**Purpose:** Bind a keyboard shortcut to a plugin action.

**Registration:**
```nitpick
use "../../src/plugin/ext_hotkey.npk".*;

// In plugin_init:
int64:hk = raw api_register_hotkey(api,
    "plugin.my-plugin.search",   // unique namespaced action
    "Ctrl+Shift+F",              // default chord
    "Search with my plugin",     // description shown in UI
    2i64);                       // callback_id
if (hk < 0i64) {
    drop(api_log(api, "warn", "hotkey conflict, binding skipped"));
}
```

**Signature:**
```
api_register_hotkey(api, action: string, chord: string,
                    description: string, callback_id: int64) → int64
api_unregister_hotkey(api, action: string) → NIL
```

**Gotchas:**
- `action` must be globally unique (namespaced: `"plugin.my-plugin.something"`).
- Returns -1 if `action` is already registered. Check and warn in your init.
- The chord is a default; users can override it in `hotkeys.toml`.
- Callback dispatch fires via the event bus or the hotkey engine — poll
  `hk_find_action` in your tick if needed.

---

### 3. ToolbarButton

**Purpose:** Add a button to Nitty's main toolbar.

**Registration:**
```nitpick
use "../../src/plugin/ext_toolbar.npk".*;

int64:tb = raw api_register_toolbar_button(api,
    "my-plugin.run",    // unique id
    "▶",               // icon (unicode or named)
    "Run my plugin",    // tooltip
    "right",            // position: "left" | "right"
    3i64);              // callback_id
```

**Signature:**
```
api_register_toolbar_button(api, id: string, icon: string,
                            tooltip: string, position: string,
                            callback_id: int64) → int64
api_unregister_toolbar_button(api, id: string) → NIL
api_set_toolbar_button_state(api, id: string, enabled: int64) → NIL
```

---

### 4. TabHeader

**Purpose:** Add a badge or indicator widget to the tab header strip.

**Registration:**
```nitpick
use "../../src/plugin/ext_tab_header.npk".*;

int64:th = raw api_register_tab_header(api,
    "my-plugin.badge",  // unique id
    "right",            // position: "left" | "right"
    4i64);              // callback_id
```

**Signature:**
```
api_register_tab_header(api, id: string, position: string,
                         callback_id: int64) → int64
api_unregister_tab_header(api, id: string) → NIL
```

---

### 5. ContextMenuProvider

**Purpose:** Add an item to the right-click context menu in the terminal.

**Registration:**
```nitpick
use "../../src/plugin/ext_context_menu.npk".*;

int64:cm = raw api_register_context_menu_item(api,
    "my-plugin.search-selected",   // unique id
    "Search with My Plugin",       // display label
    "🔍",                          // icon (unicode or "")
    "custom",                      // group: "edit" | "custom" | etc.
    5i64);                         // callback_id
```

**Signature:**
```
api_register_context_menu_item(api, id: string, label: string,
                               icon: string, group: string,
                               callback_id: int64) → int64
api_unregister_context_menu_item(api, id: string) → NIL
```

---

### 6. ConnectionProvider

**Purpose:** Register a new connection type (shown in the Connection Manager).

**Registration:**
```nitpick
use "../../src/plugin/ext_connection.npk".*;

int64:cp = raw api_register_connection_provider(api,
    "my-protocol",           // type_name (internal key)
    "My Protocol",           // display_name (shown in UI)
    "🔌",                    // icon
    6i64);                   // callback_id
```

**Signature:**
```
api_register_connection_provider(api, type_name: string, display_name: string,
                                 icon: string, callback_id: int64) → int64
api_unregister_connection_provider(api, type_name: string) → NIL
```

**Gotchas:**
- The callback receives a connection request; your plugin is responsible for
  opening the PTY/fd and returning it to the pane manager.
- `type_name` must be globally unique (e.g., `"my-plugin.my-protocol"`).

---

### 7. SettingsPanel

**Purpose:** Define configurable settings that appear in the Plugin Manager's
settings form. Each setting becomes a field the user can view and edit.

**Registration:**
```nitpick
use "../../src/plugin/ext_settings.npk".*;

// Register each setting individually — call once per key
drop(api_register_setting(api,
    "my-plugin",           // plugin_name (matches manifest name)
    "my-plugin.keywords",  // key
    "Highlight Keywords",  // human-readable label
    "string",              // type: "string" | "bool" | "int" | "color"
    "error,warn,todo"));   // default value
```

**Signature:**
```
api_register_setting(api, plugin_name: string, key: string,
                     label: string, type: string,
                     default_value: string) → int64
```

**Supported types:**

| Type | UI widget | Example default |
|---|---|---|
| `"string"` | text entry | `"hello"` |
| `"bool"` | checkbox | `"true"` / `"false"` |
| `"int"` | number entry | `"42"` |
| `"color"` | color picker | `"#ffcc00"` |

**Reading settings at runtime:**
```nitpick
string:val = raw api_get_config(api, "my-plugin.keywords");
// Note: api_get_config is stubbed in v0.10.x; returns "" until v0.11.
// Use your own config file reading in the interim.
```

---

## Event Bus

Subscribe to Nitty's built-in events or emit custom events between plugins.

**Built-in event strings:**

| Constant | String | Fires when |
|---|---|---|
| `EV_TAB_CREATED` | `"nitty.tab.created"` | A new tab is opened |
| `EV_TAB_CLOSED` | `"nitty.tab.closed"` | A tab is closed |
| `EV_TAB_ACTIVATED` | `"nitty.tab.activated"` | The active tab changes |
| `EV_PANE_CREATED` | `"nitty.pane.created"` | A pane split is created |
| `EV_PANE_CLOSED` | `"nitty.pane.closed"` | A pane is closed |
| `EV_PANE_FOCUSED` | `"nitty.pane.focused"` | Focus moves to a pane |
| `EV_CONNECTION_OPENED` | `"nitty.connection.opened"` | SSH/serial/telnet connected |
| `EV_CONNECTION_CLOSED` | `"nitty.connection.closed"` | Connection disconnected |
| `EV_CONFIG_CHANGED` | `"nitty.config.changed"` | Config file reloaded |

**Subscribing:**
```nitpick
// In plugin_init:
int64:sub = raw api_subscribe(api, "nitty.config.changed", 10i64);
// callback_id=10 will be delivered when config changes
```

**Emitting a custom event:**
```nitpick
drop(api_emit(api, "my-plugin.keyword-matched", "found: error"));
```

**Unsubscribing (on destroy):**
```nitpick
// event_bus_remove_plugin is called automatically for your slot on disable.
// Manual: use event_bus_unsubscribe if you need to unsubscribe early.
```

---

## Plugin API Reference Summary

> See [`docs/plugin-api-reference.md`](plugin-api-reference.md) for the full reference.

| Function | Returns | Description |
|---|---|---|
| `api_make(slot)` | `PluginAPI` | Construct API handle (called by loader) |
| `api_get_slot(api)` | `int64` | Registry slot of this plugin |
| `api_get_config(api, key)` | `string` | Read config value (stub in v0.10) |
| `api_get_active_pane(api)` | `int64` | Active pane id (stub returns 0) |
| `api_write_to_terminal(api, pane, data)` | `NIL` | Write data to pane (stub logs) |
| `api_show_notification(api, title, body)` | `NIL` | Show notification (stub logs) |
| `api_log(api, level, message)` | `NIL` | Structured log to stdout |
| `api_subscribe(api, event_type, callback_id)` | `int64` | Subscribe to event |
| `api_emit(api, event_type, data)` | `NIL` | Emit event to subscribers |
| `api_get_data_dir(api)` | `string` | Persistent data directory (stub in v0.10) |
| `api_get_cache_dir(api)` | `string` | Cache directory (stub in v0.10) |

---

## Data & Cache Directories

> **v0.10 status:** `api_get_data_dir` and `api_get_cache_dir` return `""` in
> the current release. Real path resolution (`~/.config/nitty/plugins/<name>/data/`
> and `~/.cache/nitty/plugins/<name>/`) is planned for v0.11.

**Workaround for v0.10:** Use `nitpick-env` to read `$HOME` and construct paths
manually:
```nitpick
// Until api_get_data_dir is implemented:
string:home   = raw sys_getenv("HOME");
string:data   = string_concat(home, "/.config/nitty/plugins/my-plugin/data/");
```

---

## Logging

Use structured logging so your messages appear correctly in Nitty's log output:

```nitpick
drop(api_log(api, "info",  "Plugin initialized successfully"));
drop(api_log(api, "warn",  "Could not read config key: my-plugin.keywords"));
drop(api_log(api, "error", "Failed to open connection"));
```

Output format: `[plugin:<slot>] [<level>] <message>`

**Log levels:** `"info"` | `"warn"` | `"error"`

---

## Configuration Access

> **v0.10 status:** `api_get_config` is stubbed and always returns `""`.

Until `api_get_config` is fully implemented (v0.11), store plugin configuration
in a TOML or YAML file inside your plugin directory and parse it with
`nitpick-toml` or `nitpick-yaml`.

---

## Error Handling

Follow these patterns for robust plugins:

**Always check registration return values:**
```nitpick
int64:idx = raw api_register_hotkey(api, "plugin.mine.do-thing",
                                    "Ctrl+Shift+G", "Do the thing", 1i64);
if (idx < 0i64) {
    drop(api_log(api, "warn", "hotkey conflict — binding unavailable"));
    // Continue without the hotkey rather than failing entirely
}
```

**Never crash in plugin_init:**
- Wrap operations that might fail in explicit checks.
- If an extension point registration fails, log a warning and continue.
- Only return early if the failure is truly fatal to your plugin's function.

**Implement `failsafe`:**
```nitpick
func:failsafe = int32(tbb32:_err) {
    // Called by the runtime on unhandled errors
    exit 1i32;
};
```

**Guard against missing config:**
```nitpick
string:val = raw api_get_config(api, "my-plugin.timeout");
if (string_length(val) == 0i64) {
    val = "30";  // use default
}
```

---

## Testing Plugins

Nitty provides `src/plugin/test_utils.npk` with mock helpers for testing
plugins in isolation (without a running GTK4 window).

**Pattern:**
```nitpick
use "../../src/plugin/test_utils.npk".*;
use "./main.npk".*;   // your plugin

func:main = int32() {
    // Set up mock environment
    drop(mock_reset());
    drop(ext_connection_init());
    drop(ext_hotkey_init());
    drop(ext_settings_init());
    drop(ext_terminal_init());

    // Create a mock API handle for slot 0
    PluginAPI:api = raw mock_api_new(0i64);

    // Call your plugin init
    drop(plugin_init(api));

    // Assert registrations happened
    int64:dec_count = raw mock_get_registered_decorator_count();
    if (dec_count != 1i64) { println("FAIL: expected 1 decorator"); exit 1i32; }

    println("All tests passed!");
    exit 0i32;
};
```

See `examples/plugins/word-highlighter/test_highlighter.npk` for a complete
test example.

---

## Publishing

**Packaging:**
1. Ensure your `plugin.yaml` is accurate and complete.
2. Compile your `main.npk` for the target platform.
3. Bundle the directory: `tar -czf my-plugin-1.0.0.tar.gz my-plugin/`

**Distribution options:**
- Share the archive directly (users extract to `~/.config/nitty/plugins/`).
- Submit to the Nitty plugin registry (future: `nitty-packages` repository).

**Installation (user side):**
```sh
cd ~/.config/nitty/plugins/
tar -xzf my-plugin-1.0.0.tar.gz
# Restart Nitty — the plugin will be discovered and loaded automatically
```

> **Note:** The Plugin Manager's "Install from Archive" button (Ctrl+Shift+P)
> will handle this automatically in a future release (v0.10.5+).
