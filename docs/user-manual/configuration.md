# Configuration Guide

Nitty's primary configuration is stored in a single YAML file located at `~/.config/nitty/config.yaml`. Nitty supports hot-reloading for most settings, meaning changes saved to this file will immediately take effect without requiring a restart.

## Configuration File Format

Nitty uses standard YAML syntax. The configuration file is organized into sections.

```yaml
[general]
close_on_exit = 1
confirm_on_close = 1

[terminal]
font_family = "Monospace"
font_size = 12
scrollback_lines = 3000
```

*(Note: While Nitty reads this file with a custom parser that currently uses TOML-like bracket section syntax under the hood, standard key-value assignments are supported.)*

## Configuration Reference

Below is a complete list of all supported settings, organized by section.

### `[general]`
- **`close_on_exit`** (bool, default: `1`): Close the tab or pane automatically when the shell/process exits.
- **`confirm_on_close`** (bool, default: `1`): Prompt for confirmation before closing the entire application window if multiple tabs/panes are active.

### `[terminal]`
- **`font_family`** (string, default: `"Monospace"`): The font family to use for terminal rendering.
- **`font_size`** (integer, default: `12`): The font size in points (valid range: 6 - 72).
- **`scrollback_lines`** (integer, default: `3000`): The maximum number of lines to keep in the scrollback history per pane (valid range: 0 - 3000).
- **`shell`** (string, default: `""`): The default shell to launch for local sessions. If empty, Nitty uses the user's default shell from `/etc/passwd`.
- **`columns`** (integer, default: `80`): Initial window width in characters.
- **`rows`** (integer, default: `24`): Initial window height in characters.
- **`cursor_style`** (enum, default: `"block"`): The cursor shape. Valid options: `"block"`, `"underline"`, `"bar"`.
- **`cursor_blink`** (bool, default: `1`): Whether the cursor should blink.
- **`copy_on_select`** (bool, default: `0`): If `1`, text is automatically copied to the system clipboard upon selection release.
- **`warn_multiline_paste`** (bool, default: `1`): Warn before pasting text containing multiple lines to prevent accidental execution.
- **`right_click_paste`** (bool, default: `0`): If `1`, right-clicking pastes the clipboard contents instead of opening the context menu.
- **`link_handler`** (string, default: `"xdg-open"`): Command used to open clicked URLs.
- **`link_click_modifier`** (string, default: `"none"`): Modifier key required to click links (e.g., `"Ctrl"`).
- **`bell_mode`** (enum, default: `"audible"`): How terminal bells (BEL, `\a`) are handled. Options: `"audible"`, `"visual"`, `"notification"`, `"disabled"`.
- **`bell_notify`** (bool, default: `0`): If `1`, show a desktop toast notification on bell.
- **`visual_bell_duration`** (integer, default: `100`): Flash duration in ms for the visual bell.
- **`activity_notification`** (bool, default: `0`): Notify when output occurs in a background tab.
- **`activity_silence_threshold`** (integer, default: `5000`): Milliseconds of silence required before activity triggers a new notification.
- **`process_completion_notification`** (bool, default: `1`): Notify when a long-running command finishes in a background tab.
- **`process_min_duration`** (integer, default: `10000`): Minimum runtime (ms) for a command to trigger a completion notification.

### `[appearance]`
- **`opacity`** (float, default: `1000`): Window background opacity represented as a fixed-point integer out of 1000. `1000` is fully opaque (1.0), `850` is 85% opacity.
- **`theme`** (string, default: `"default"`): The active color theme. Supports built-in themes (e.g., `"default"`, `"solarized-dark"`, `"high-contrast-dark"`).

### `[window]`
- **`always_on_top`** (bool, default: `0`): Keep the Nitty window above all other windows.
- **`decorated`** (bool, default: `1`): Show system title bar and window borders.
- **`icon_name`** (string, default: `"utilities-terminal"`): The XDG icon name for the application.

### `[quake]`
Quake mode provides a persistent, drop-down terminal overlaid on top of your screen.
- **`enabled`** (bool, default: `0`): Enable Quake mode. If `1`, Nitty starts hidden and is toggled via the hotkey.
- **`hotkey`** (string, default: `"F12"`): The global shortcut to toggle the Quake window.
- **`width_percent`** (integer, default: `100`): Window width as a percentage of the screen width (20 - 100).
- **`height_percent`** (integer, default: `50`): Window height as a percentage of the screen height (10 - 100).
- **`position`** (enum, default: `"top"`): Where the window docks. Options: `"top"`, `"bottom"`.
- **`animation_duration`** (integer, default: `150`): Slide animation duration in milliseconds.

### `[zmodem]`
- **`auto_detect`** (bool, default: `1`): Automatically intercept Zmodem transfer requests from remote servers (e.g. `sz`).
- **`download_dir`** (string, default: `"~/Downloads"`): Destination folder for received files.

### `[a11y]` (Accessibility)
- **`enabled`** (bool, default: `1`): Initialize the AT-SPI accessibility layer for screen readers.
- **`screen_reader_mode`** (bool, default: `0`): Suppress purely visual effects and optimize for screen readers.
- **`announce_output`** (bool, default: `0`): Force live-region announcements for every new terminal output block.
- **`high_contrast`** (bool, default: `0`): Force high-contrast themes regardless of the system GTK preference.
- **`reduce_motion`** (bool, default: `0`): Disable cursor blink and animations (like Quake slide).

## Profiles

Nitty allows you to save connection settings into Profiles. Profiles are managed primarily via the UI (Connection Manager) but are saved to disk under `~/.config/nitty/profiles/`. 

Each profile defines connection type (SSH, Serial, Telnet), target hosts, authentication methods, and customized colors or fonts specific to that session.

## Themes

Nitty includes multiple built-in themes. You can specify a theme by name in `config.yaml` under `appearance.theme`. 

Built-in themes include:
- `default`
- `solarized-dark` / `solarized-light`
- `dracula`
- `nord`
- `high-contrast-dark` / `high-contrast-light` (Strict WCAG AAA compliant palettes)

## Environment Variables

Nitty respects the following environment variables:
- `NITTY_CONFIG_DIR`: Override the path to the configuration directory (defaults to `~/.config/nitty`).
- `EDITOR`: Used when opening files via plugins or fallback editors.
