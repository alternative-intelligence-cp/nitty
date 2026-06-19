# Nitty Documentation

**Nitty** — a Nitpick-native terminal emulator with a first-class plugin system.

---

## Developer Documentation

| Document | Description |
|---|---|
| [Plugin Documentation Index](plugins/README.md) | Start here for plugin development |
| [Plugin Development Guide](plugin-development-guide.md) | Full guide for plugin authors |
| [Plugin API Reference](plugin-api-reference.md) | Complete function reference |
| [Plugin Manifest Specification](plugin-manifest.md) | `plugin.yaml` format reference |
| [GTK4 FFI Reference](gtk4_ffi.md) | C shim boundary docs (for core development) |
| [CI/CD Pipeline](ci-cd.md) | How CI works and how to publish a release |

> **v0.15.0 note**: Per-plugin settings persistence (`cfg_set_plugin_setting`, `config_save_plugin_settings`) is fully implemented. The FAQ reference to `api_get_config` being a stub is no longer accurate — see the Plugin API Reference for the current settings API.

