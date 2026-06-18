# Nitty Plugin API Reference

**API version:** 0.10  
**Applies to:** Nitty v0.10.5+

This document is a complete reference for every function available to plugin authors.
See the [Plugin Development Guide](plugin-development-guide.md) for concepts and
usage patterns.

---

## Table of Contents

- [PluginAPI Core (`api.npk`)](#pluginapi-core)
- [Hotkey Registration (`ext_hotkey.npk`)](#hotkey-registration)
- [Toolbar Button Registration (`ext_toolbar.npk`)](#toolbar-button-registration)
- [Tab Header Registration (`ext_tab_header.npk`)](#tab-header-registration)
- [Context Menu Registration (`ext_context_menu.npk`)](#context-menu-registration)
- [Terminal Decorator Registration (`ext_terminal.npk`)](#terminal-decorator-registration)
- [Connection Provider Registration (`ext_connection.npk`)](#connection-provider-registration)
- [Settings Registration (`ext_settings.npk`)](#settings-registration)
- [Config Watch Registration (`ext_config.npk`)](#config-watch-registration)
- [Event Bus (`event_bus.npk`)](#event-bus)
- [Plugin Registry (`plugin_registry.npk`)](#plugin-registry)
- [Plugin Manager (`plugin_manager.npk`)](#plugin-manager)

---

## PluginAPI Core

> Source: `src/plugin/api.npk`

### Struct: `PluginAPI`

```nitpick
pub struct:PluginAPI = {
    int64:slot;     // registry slot of this plugin (0–63)
    int64:context;  // reserved, always 0
};
```

---

### `api_make`

```
pub func:api_make = PluginAPI(int64:slot)
```

Construct a `PluginAPI` handle for the given registry slot. Called by the loader — plugin authors do not call this directly.

| Parameter | Type | Description |
|---|---|---|
| `slot` | `int64` | Registry slot index (0–63) |

**Returns:** `PluginAPI` with `slot` set and `context=0`.

---

### `api_get_slot`

```
pub func:api_get_slot = int64(PluginAPI:api)
```

Returns the registry slot of this plugin. Useful when you need to store your own slot index for later use.

**Returns:** `int64` — the registry slot.

---

### `api_get_config`

```
pub func:api_get_config = string(PluginAPI:api, string:key)
```

Read a configuration value by key.

> ⚠️ **v0.10 stub** — always returns `""`. Full implementation planned for v0.11 when the config schema is stable.

**Workaround:** Parse your own config file using `nitpick-toml` or `nitpick-yaml`.

| Parameter | Type | Description |
|---|---|---|
| `api` | `PluginAPI` | Plugin API handle |
| `key` | `string` | Config key e.g. `"my-plugin.timeout"` |

**Returns:** `string` — value or `""` if not found / stubbed.

---

### `api_get_active_pane`

```
pub func:api_get_active_pane = int64(PluginAPI:api)
```

Returns the active pane ID.

> ⚠️ **v0.10 stub** — always returns `0`. Real GUI integration in v0.11.

**Returns:** `int64` — pane ID (0 in v0.10).

---

### `api_write_to_terminal`

```
pub func:api_write_to_terminal = NIL(PluginAPI:api, int64:pane, string:data)
```

Write data to a terminal pane (e.g., inject input or output).

> ⚠️ **v0.10 stub** — logs to stdout instead of writing to PTY.

| Parameter | Type | Description |
|---|---|---|
| `api` | `PluginAPI` | Plugin API handle |
| `pane` | `int64` | Target pane ID |
| `data` | `string` | Data to write |

---

### `api_show_notification`

```
pub func:api_show_notification = NIL(PluginAPI:api, string:title, string:body)
```

Show a desktop notification to the user.

> ⚠️ **v0.10 stub** — logs to stdout instead of showing a real notification.

| Parameter | Type | Description |
|---|---|---|
| `api` | `PluginAPI` | Plugin API handle |
| `title` | `string` | Notification title |
| `body` | `string` | Notification body text |

---

### `api_log`

```
pub func:api_log = NIL(PluginAPI:api, string:level, string:message)
```

Write a structured log entry to stdout. This is the primary logging mechanism for plugins.

| Parameter | Type | Description |
|---|---|---|
| `api` | `PluginAPI` | Plugin API handle |
| `level` | `string` | `"info"` \| `"warn"` \| `"error"` |
| `message` | `string` | Log message |

**Output format:** `[plugin:<slot>] [<level>] <message>`

**Example:**
```nitpick
drop(api_log(api, "info",  "initialized successfully"));
drop(api_log(api, "warn",  "config key missing, using default"));
drop(api_log(api, "error", "connection refused"));
```

---

### `api_subscribe`

```
pub func:api_subscribe = int64(PluginAPI:api, string:event_type, int64:callback_id)
```

Subscribe to a named event. When the event fires, `callback_id` is used to
identify your handler. Maximum 64 total subscriptions across all plugins.

| Parameter | Type | Description |
|---|---|---|
| `api` | `PluginAPI` | Plugin API handle |
| `event_type` | `string` | Event name (see [Built-in Events](#built-in-events)) |
| `callback_id` | `int64` | Your identifier for this subscription |

**Returns:** `int64` — subscription index (≥ 0) on success, `-1` if full or duplicate.

**Example:**
```nitpick
int64:sub = raw api_subscribe(api, "nitty.config.changed", 10i64);
if (sub < 0i64) { drop(api_log(api, "warn", "subscribe failed")); }
```

---

### `api_emit`

```
pub func:api_emit = NIL(PluginAPI:api, string:event_type, string:data)
```

Emit a named event to all subscribers.

> ⚠️ **v0.10 stub** — logs delivery count. Real dispatch requires C shim (v0.10.3+).

| Parameter | Type | Description |
|---|---|---|
| `api` | `PluginAPI` | Plugin API handle |
| `event_type` | `string` | Event name |
| `data` | `string` | Free-form payload string |

---

### `api_get_data_dir`

```
pub func:api_get_data_dir = string(PluginAPI:api)
```

Returns the plugin's persistent data directory path.

> ⚠️ **v0.10 stub** — returns `""`. Real path: `~/.config/nitty/plugins/<name>/data/` in v0.11.

---

### `api_get_cache_dir`

```
pub func:api_get_cache_dir = string(PluginAPI:api)
```

Returns the plugin's cache directory path.

> ⚠️ **v0.10 stub** — returns `""`. Real path: `~/.cache/nitty/plugins/<name>/` in v0.11.

---

## Hotkey Registration

> Source: `src/plugin/ext_hotkey.npk`  
> Capacity: 64 hotkeys total across all plugins.

### `api_register_hotkey`

```
pub func:api_register_hotkey = int64(
    PluginAPI:api,
    string:action,
    string:chord,
    string:description,
    int64:callback_id
)
```

Register a keyboard shortcut mapped to a plugin callback.

| Parameter | Type | Description |
|---|---|---|
| `api` | `PluginAPI` | Plugin API handle |
| `action` | `string` | Unique namespaced action name e.g. `"plugin.my-plugin.do-thing"` |
| `chord` | `string` | Default key chord e.g. `"Ctrl+Shift+F"` |
| `description` | `string` | Human-readable description for the Settings UI |
| `callback_id` | `int64` | Your dispatch identifier |

**Returns:** `int64` — slot index (≥ 0) on success, `-1` if full or duplicate action.

> Action names must be globally unique. Use the namespace pattern: `"plugin.<plugin-name>.<action>"`.

---

### `api_unregister_hotkey`

```
pub func:api_unregister_hotkey = NIL(PluginAPI:api, string:action)
```

Remove a hotkey registration by action name. Typically called from `plugin_destroy()`.

---

## Toolbar Button Registration

> Source: `src/plugin/ext_toolbar.npk`  
> Capacity: 32 buttons total.

### `api_register_toolbar_button`

```
pub func:api_register_toolbar_button = int64(
    PluginAPI:api,
    string:id,
    string:icon,
    string:tooltip,
    string:position,
    int64:callback_id
)
```

| Parameter | Type | Description |
|---|---|---|
| `id` | `string` | Unique button ID |
| `icon` | `string` | Unicode icon or icon name |
| `tooltip` | `string` | Hover tooltip text |
| `position` | `string` | `"left"` or `"right"` |
| `callback_id` | `int64` | Your dispatch identifier |

**Returns:** `int64` — index (≥ 0) or `-1` if full/duplicate.

---

### `api_unregister_toolbar_button`

```
pub func:api_unregister_toolbar_button = NIL(PluginAPI:api, string:id)
```

---

### `api_set_toolbar_button_state`

```
pub func:api_set_toolbar_button_state = NIL(PluginAPI:api, string:id, int64:enabled)
```

Enable (`1`) or disable (`0`) a toolbar button dynamically.

---

## Tab Header Registration

> Source: `src/plugin/ext_tab_header.npk`  
> Capacity: 16 tab header widgets total.

### `api_register_tab_header`

```
pub func:api_register_tab_header = int64(
    PluginAPI:api,
    string:id,
    string:position,
    int64:callback_id
)
```

| Parameter | Type | Description |
|---|---|---|
| `id` | `string` | Unique widget ID |
| `position` | `string` | `"before_title"` or `"after_title"` |
| `callback_id` | `int64` | Your dispatch identifier |

**Returns:** `int64` — index (≥ 0) or `-1`.

---

### `api_unregister_tab_header`

```
pub func:api_unregister_tab_header = NIL(PluginAPI:api, string:id)
```

---

## Context Menu Registration

> Source: `src/plugin/ext_context_menu.npk`  
> Capacity: 32 items total.

### `api_register_context_menu_item`

```
pub func:api_register_context_menu_item = int64(
    PluginAPI:api,
    string:id,
    string:label,
    string:icon,
    string:group,
    int64:callback_id
)
```

| Parameter | Type | Description |
|---|---|---|
| `id` | `string` | Unique item ID |
| `label` | `string` | Display label shown in the menu |
| `icon` | `string` | Unicode icon or `""` for no icon |
| `group` | `string` | Separator grouping: `"edit"` \| `"session"` \| `"custom"` |
| `callback_id` | `int64` | Your dispatch identifier |

**Returns:** `int64` — index (≥ 0) or `-1`.

---

### `api_unregister_context_menu_item`

```
pub func:api_unregister_context_menu_item = NIL(PluginAPI:api, string:id)
```

---

## Terminal Decorator Registration

> Source: `src/plugin/ext_terminal.npk`  
> Capacity: 16 decorators total. **One per plugin slot.**

### `api_register_terminal_decorator`

```
pub func:api_register_terminal_decorator = int64(PluginAPI:api, int64:callback_id)
```

Register this plugin as a terminal output decorator. Each plugin may register
exactly one decorator. Callbacks fire when terminal data arrives.

| Parameter | Type | Description |
|---|---|---|
| `api` | `PluginAPI` | Plugin API handle |
| `callback_id` | `int64` | Your dispatch identifier |

**Returns:** `int64` — index (≥ 0) or `-1` if already registered or full.

> ⚠️ **v0.10 dispatch stub** — `ext_terminal_dispatch_data` logs but does not invoke plugin callbacks. Real dispatch via C shim in v0.10.3+.

---

### `api_unregister_terminal_decorator`

```
pub func:api_unregister_terminal_decorator = NIL(PluginAPI:api)
```

No `id` parameter — identified by plugin slot.

---

## Connection Provider Registration

> Source: `src/plugin/ext_connection.npk`  
> Capacity: 8 connection types total (includes 3 built-ins: ssh, serial, telnet).

### `api_register_connection_provider`

```
pub func:api_register_connection_provider = int64(
    PluginAPI:api,
    string:type_name,
    string:display_name,
    string:icon,
    int64:callback_id
)
```

Register a new connection type shown in the Connection Manager.

| Parameter | Type | Description |
|---|---|---|
| `type_name` | `string` | Unique internal key e.g. `"my-plugin.my-protocol"` |
| `display_name` | `string` | UI label e.g. `"My Protocol"` |
| `icon` | `string` | Unicode icon |
| `callback_id` | `int64` | Your dispatch identifier |

**Returns:** `int64` — index (≥ 0) or `-1` if full (max 8) or duplicate.

> **Capacity note:** 3 of the 8 slots are used by built-in providers (SSH, Serial, Telnet). This leaves 5 slots for third-party plugins.

---

### `api_unregister_connection_provider`

```
pub func:api_unregister_connection_provider = NIL(PluginAPI:api, string:type_name)
```

---

## Settings Registration

> Source: `src/plugin/ext_settings.npk`  
> Capacity: 64 setting definitions total.

### `api_register_setting`

```
pub func:api_register_setting = int64(
    PluginAPI:api,
    string:plugin_name,
    string:key,
    string:label,
    string:type_name,
    string:default_value
)
```

Define a configurable setting that appears in the Plugin Manager's settings form.

| Parameter | Type | Description |
|---|---|---|
| `plugin_name` | `string` | Plugin namespace (must match manifest `name`) |
| `key` | `string` | Config key e.g. `"my-plugin.timeout"` |
| `label` | `string` | Human-readable UI label |
| `type_name` | `string` | `"string"` \| `"bool"` \| `"int"` \| `"select"` |
| `default_value` | `string` | Serialized default e.g. `"30"`, `"true"`, `"#ffcc00"` |

**Returns:** `int64` — global slot index (≥ 0) or `-1` if full.

**Example:**
```nitpick
drop(api_register_setting(api, "my-plugin", "my-plugin.keywords",
    "Highlight Keywords", "string", "error,warn,todo"));
drop(api_register_setting(api, "my-plugin", "my-plugin.enabled",
    "Enable highlighting", "bool", "true"));
drop(api_register_setting(api, "my-plugin", "my-plugin.max-matches",
    "Max Matches Per Line", "int", "10"));
```

---

## Config Watch Registration

> Source: `src/plugin/ext_config.npk`  
> Capacity: 32 config watches, 32 config defaults.

### `api_register_config_default`

```
pub func:api_register_config_default = NIL(PluginAPI:api, string:key, string:default_value)
```

Register a default value for a config key. Applied when the key is missing from the user's config file.

---

### `api_on_config_change`

```
pub func:api_on_config_change = int64(PluginAPI:api, string:key, int64:callback_id)
```

Watch a specific config key for changes.

> ⚠️ **v0.10.1 stub** — records the watch but does not invoke callbacks on change. Use `api_subscribe(api, "nitty.config.changed", callback_id)` as the current alternative.

**Returns:** `int64` — watch index (≥ 0) or `-1`.

---

## Event Bus

> Source: `src/plugin/event_bus.npk`  
> Capacity: 64 total subscriptions across all plugins.

### Built-in Events

| Constant | String | Fires when |
|---|---|---|
| `EV_TAB_CREATED` | `"nitty.tab.created"` | A new tab is opened |
| `EV_TAB_CLOSED` | `"nitty.tab.closed"` | A tab is closed |
| `EV_TAB_ACTIVATED` | `"nitty.tab.activated"` | The active tab changes |
| `EV_PANE_CREATED` | `"nitty.pane.created"` | A pane split is created |
| `EV_PANE_CLOSED` | `"nitty.pane.closed"` | A pane is closed |
| `EV_PANE_FOCUSED` | `"nitty.pane.focused"` | Focus moves to a pane |
| `EV_CONNECTION_OPENED` | `"nitty.connection.opened"` | A connection is established |
| `EV_CONNECTION_CLOSED` | `"nitty.connection.closed"` | A connection drops |
| `EV_CONFIG_CHANGED` | `"nitty.config.changed"` | Config file is reloaded |

### `event_bus_emit_named`

```
pub func:event_bus_emit_named = NIL(string:source, string:event_type, string:data)
```

Internal emit function. Plugin authors should use `api_emit(api, event_type, data)`.

---

## Plugin Registry

> Source: `src/plugin/plugin_registry.npk`

### Plugin State Constants

```nitpick
PLUGIN_STATE_UNLOADED    = 0   // not yet loaded
PLUGIN_STATE_LOADED      = 1   // manifest matched
PLUGIN_STATE_INITIALIZED = 2   // plugin_init called
PLUGIN_STATE_ACTIVE      = 3   // fully running
PLUGIN_STATE_ERROR       = 4   // init failed
PLUGIN_STATE_DISABLED    = 5   // explicitly disabled
```

Plugins are auto-disabled after 3 errors (`PLUGIN_MAX_ERROR_COUNT = 3`).

---

## Plugin Manager

> Source: `src/plugin/plugin_manager.npk`

### `plugin_manager_enable`

```
pub func:plugin_manager_enable = int64(string:name)
```

Enable a disabled plugin by name.

**Returns:** `int64` — `0`=success, `-1`=not found, `-2`=already active.

---

### `plugin_manager_disable`

```
pub func:plugin_manager_disable = int64(string:name)
```

Disable a plugin by name. Cascades to disable dependent plugins first.

**Returns:** `int64` — `0`=success, `-1`=not found.

---

### `plugin_manager_get_state`

```
pub func:plugin_manager_get_state = int64(string:name)
```

Get current state (`PLUGIN_STATE_*`) for a plugin by name.

---

### `plugin_manager_active_count`

```
pub func:plugin_manager_active_count = int64()
```

Count of currently active plugins.
