# Module Reference

This document lists all `.npk` files in the Nitty source tree, organized by their directory.

## Core (`src/core/`)
Manages global state, application lifecycle, and hotkeys.
- **`app.npk`**: Application entry point and initialization sequence.
- **`constants.npk`**: Global constants used across layers.
- **`global_hotkey.npk`**: OS-level global hotkey registration (e.g., Quake mode).
- **`hotkey.npk`**: The multi-chord keyboard shortcut engine.
- **`pane.npk`**: Pane struct definition, lifecycle, and VT parser wiring.
- **`session.npk`**: PTY session and process management.
- **`split_tree.npk`**: BSP tree logic for organizing split panes within a tab.
- **`tab.npk`**: Tab struct and title/activity state.
- **`tab_manager.npk`**: Global collection of tabs and focus management.
- **`zmodem.npk`**: Zmodem auto-detection and trigger logic.

## Configuration (`src/config/`)
Handles user settings and themes.
- **`config.npk`**: In-memory config struct and getter accessors.
- **`schema.npk`**: The definitive definition of all valid configuration keys and defaults.
- **`profile.npk`**: Connection profile definitions (SSH/Serial/Telnet).
- **`theme.npk`**: Color palette application logic.
- **`builtin_themes.npk`**: Hardcoded ANSI palettes (e.g., Solarized, High Contrast).
- **`color_schemes.npk`**: Extracted palette structures.
- **`paths.npk`**: XDG base directory resolution.
- **`window_state.npk`**: Persistence of window geometry across restarts.
- **`watcher.npk`**: Inotify watcher for hot-reloading `config.yaml`.

## GUI (`src/gui/`)
All GTK4 integration and UI components.
- **`accessibility.npk`**: AT-SPI integration for screen readers.
- **`clipboard.npk`**: System clipboard read/write via GTK.
- **`connection_manager.npk`**: Sidebar UI for managing SSH/Serial profiles.
- **`dirty_tracker.npk`**: Bitset tracking grid cells that need re-rendering.
- **`gtk4_ffi.npk`**: The massive `extern` block binding Nitpick to `shim/gtk4_shim.c`.
- **`input.npk`**: GTK key event translation to VT escape sequences.
- **`renderer.npk`**: Frame synchronization logic sending Nitpick cells to C.
- **`terminal_widget.npk`**: The main terminal view widget and draw loop.
- **`window.npk`**: Main window creation and layout hierarchy.
- **`tab_bar.npk`**: Custom tab bar drawing and interaction.
- **`tab_context_menu.npk`**: Right-click context menus for tabs.
- *(And 20+ other UI component modules for settings, sftp, split view, search, etc.)*

## Terminal (`src/terminal/`)
The VT100/Xterm emulator core.
- **`vt_parser.npk`**: The state machine for parsing escape sequences.
- **`terminal_state.npk`**: In-memory representation of the terminal grid and cursor.
- **`grid.npk`**: Fixed-size 2D array of character cells.
- **`scrollback.npk`**: Ring buffer for off-screen lines.
- **`csi_dispatch.npk`**, **`osc_dispatch.npk`**: Handlers for specific escape codes.
- **`pty.npk`**, **`pty_io.npk`**: Forkpty and read/write loop.

## Remote Protocols (`src/ssh/`, `src/serial/`, `src/telnet/`)
- **`ssh_session.npk`**: LibSSH2 connection lifecycle.
- **`ssh_vault.npk`**: Encrypted credential storage.
- **`ssh_sftp.npk`**, **`sftp_transfer.npk`**: SFTP protocol implementation.
- **`serial_port.npk`**, **`serial_session.npk`**: Termios integration for UART/USB.
- **`telnet_session.npk`**: RFC 854 IAC negotiation logic.

## Plugin System (`src/plugin/`)
Extensibility layer.
- **`plugin_manager.npk`**: UI and lifecycle for loaded plugins.
- **`loader.npk`**: Dynamic loading and execution of plugin code.
- **`plugin_safe_path.npk`**: FS jail ensuring plugins only access their data directories.
- **`api.npk`**: The `PluginAPI` struct exposed to third-party code.
- **`ext_*.npk`**: Implementation of specific extension points (terminal, settings, etc).

## Root
- **`main.npk`**: The `main()` function, bootstrapping the app and entering `gtk_main_loop`.
