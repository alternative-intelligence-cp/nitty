# Plugin Manifest Specification — `plugin.yaml`

This document describes the `plugin.yaml` manifest format for Nitty v0.10.0+ plugins.

Every plugin directory must contain a `plugin.yaml` file at its root.

---

## File Format

Plugin manifests use YAML (parsed by `nitpick-yaml`). All keys are at the
top level or one level deep under `dependencies` and `provides`.

## Required Fields

| Field | Type | Description |
|---|---|---|
| `name` | string | Unique plugin identifier (kebab-case, no spaces) |
| `version` | string | Plugin version (semver: `MAJOR.MINOR.PATCH`) |
| `api_version` | string | Nitty API version required (`MAJOR.MINOR`, e.g. `"0.10"`) |
| `entry_module` | string | Filename of the compiled Nitpick source (`main.npk`) |

## Optional Fields

| Field | Type | Default | Description |
|---|---|---|---|
| `author` | string | `""` | Plugin author name or email |
| `description` | string | `""` | Human-readable description |
| `dependencies` | list | `[]` | Dependency entries (max 8) |
| `provides` | list | `[]` | Extension point tags this plugin provides |

---

## `dependencies` Block

Each entry must have a `name` (string) and optionally a `version` constraint
(string, semver range). If `version` is omitted, any version is accepted (`"*"`).

Supported constraint syntax (via `nitpick-semver`):
- `">=1.0.0"` — at least version 1.0.0
- `"^2.0.0"` — compatible with 2.x.x (same major)
- `"~1.2.0"` — compatible with 1.2.x
- `"*"` or `""` — any version

Maximum **8 dependencies** per plugin (hard limit in the current manifest parser).

---

## `provides` Block

A list of extension point tags this plugin implements. These are informational
in v0.10.0 and are not enforced by the plugin system. Reserved for v0.10.1
when the `PluginAPI` surface is defined.

Example values:
- `ConnectionProvider` — implements a new connection type
- `ThemeProvider` — supplies a color theme
- `PanelWidget` — adds a sidebar panel

---

## Example Manifest

```yaml
name: my-ssh-extension
version: 1.2.0
api_version: "0.10"
author: "Developer <dev@example.com>"
entry_module: main.npk
description: "Extends SSH connection handling with custom auth flow"

dependencies:
  - name: nitpick-ssh-helpers
    version: ">=2.0.0"
  - name: nitpick-ui-forms
    version: "^1.0.0"

provides:
  - ConnectionProvider
```

---

## API Version Compatibility

The `api_version` field uses a `MAJOR.MINOR` format (no patch).

Nitty checks compatibility as follows:
- **Major must match exactly**: a plugin built for API `1.x` will not load on
  Nitty `0.10` and vice versa.
- **Minor must be ≤ current**: a plugin for API `0.9` will load on Nitty `0.10`,
  but a plugin for API `0.11` will be skipped on Nitty `0.10`.

| Plugin `api_version` | Nitty version | Loads? |
|---|---|---|
| `"0.10"` | `0.10.0` | ✅ |
| `"0.9"` | `0.10.0` | ✅ (minor <=) |
| `"0.11"` | `0.10.0` | ❌ (minor > current) |
| `"1.0"` | `0.10.0` | ❌ (major mismatch) |

---

## Plugin Search Directories

Nitty scans these directories (in priority order) at startup:

1. `~/.config/nitty/plugins/` — user plugins
2. `/usr/share/nitty/plugins/` — system-wide plugins
3. `./plugins/` — development plugins (current working directory)

Each subdirectory of a search directory that contains a `plugin.yaml` is
treated as a plugin. If the same plugin `name` appears in multiple directories,
the highest-priority directory wins (user > system > dev).

Additional search directories can be configured in `~/.config/nitty/nitty.toml`:
```toml
plugins.search_dirs.0 = "~/my-custom-plugins"
```

---

## Enabling / Disabling Plugins

By default, all discovered plugins are enabled. To filter:

```toml
# Only load these plugins
plugins.enabled.0 = "my-ssh-extension"
plugins.enabled.1 = "serial-logger"

# Always skip this plugin (overrides enabled list)
plugins.disabled.0 = "broken-plugin"
```

Up to **16 enabled** and **16 disabled** entries are supported.

---

## Building a Plugin

Since Nitpick is AOT-compiled, plugins are compiled into Nitty's binary. To add
a plugin:

1. Write your plugin source (`main.npk`) with a `plugin_register()` call at
   module level:
   ```nitpick
   use "/path/to/plugin_registry.npk".{plugin_register};
   int64:MY_SLOT = raw plugin_register("my-plugin");
   ```
2. Create `plugin.yaml` alongside `main.npk`.
3. Add your source to the Nitty `build.abc` compilation (or via the `use`
   import chain in an existing module).
4. Place the `plugin.yaml` in a search directory.

The plugin manager will then discover the manifest at startup and match it to
the registered entry.

---

## Dependency Load Order

Plugins are initialized in topological dependency order (dependencies before
dependents). If a circular dependency is detected, the cycle members are skipped
and a warning is logged to stdout.

---

## Error Handling

If a plugin's state cannot be verified at init time, it is marked `ERROR`.
After **3 consecutive errors**, the plugin is automatically `DISABLED`.
Errors do not affect other plugins — the plugin system is fail-tolerant.
