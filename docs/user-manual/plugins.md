# Plugins Guide

Nitty features a robust plugin system that allows developers to extend the terminal's capabilities, from simple UI tweaks to deep integration with external tools and protocols.

## What Plugins Can Do

Nitty plugins run natively within the Nitpick engine and can:
- Add new sidebar panels or floating widgets.
- Register custom hotkeys and commands.
- Read and write to the terminal scrollback buffer.
- Intercept and modify PTY data streams.
- Manage profiles and connections.
- Access the local filesystem securely via the Plugin Safe Path API.

## Finding and Installing Plugins

Nitty loads plugins from two locations:
1. **System Plugins:** Located in `/usr/share/nitty/plugins/` (or your distribution's equivalent).
2. **User Plugins:** Located in `~/.config/nitty/plugins/`.

To install a new user plugin:
1. Download the plugin folder (it should contain a `manifest.toml` file).
2. Move the folder into `~/.config/nitty/plugins/`.
3. Open the Plugin Manager and enable the plugin.

## Managing Plugins

Press `Ctrl+Shift+P` to open the **Plugin Manager** sidebar.

The Plugin Manager lists all discovered plugins. For each plugin, you can:
- **Enable/Disable:** Toggle the switch to activate or deactivate the plugin. Changes take effect immediately (hot-reload).
- **View Details:** See the plugin's description, version, and author.
- **Configure:** If the plugin exposes settings, a gear icon will appear. Clicking it opens the plugin's configuration dialog.

## Plugin Security Considerations

Nitty executes plugins within its own memory space. While Nitpick is a safe, garbage-collected language, plugins still execute code on your machine.

**Only install plugins from trusted sources.** 

To mitigate risks, Nitty enforces a **Plugin Safe Path API**. By default, plugins can only read and write files within their own dedicated directory (`~/.config/nitty/plugins/<plugin_id>/data/`). They cannot arbitrarily read your `~/.ssh/id_rsa` or modify system files unless they exploit a bug in the engine.

## Uninstalling Plugins

To uninstall a user plugin:
1. Disable it in the Plugin Manager.
2. Delete its folder from `~/.config/nitty/plugins/`.

## Built-in Plugins

Nitty may ship with some features implemented as built-in plugins (located in the system plugins directory). These are treated exactly like user plugins, but cannot be uninstalled (only disabled).

If you are a developer interested in writing your own plugins, refer to the [Plugin Development Guide](../plugin-development-guide.md) and [API Reference](../plugin-api-reference.md).
