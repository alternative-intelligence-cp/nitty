# Fresh Install & Upgrade Testing Notes — Nitty v0.99.0-beta.1

This document records the expected behavior and any observations from fresh-install and upgrade path testing. Results from a manual test run should be recorded here before tagging v1.0.

---

## Fresh Install Testing

### Expected: First Launch on Clean System

When `~/.config/nitty/` does **not** exist before launching Nitty for the first time:

1. **Config directory created:** `~/.config/nitty/` is created automatically.
2. **Default config generated:** `~/.config/nitty/config.yaml` is written with sensible defaults (see below).
3. **Default theme applied:** The Dracula theme is applied to the terminal on first launch.
4. **Shell detected:** The user's `$SHELL` environment variable is read; if unset, `bash` is used as fallback.
5. **Window size:** The window opens at 120×36 columns/rows by default.
6. **No errors:** No error dialogs or warnings are shown to the user on first launch.
7. **Hotkeys work:** All default hotkeys (`Ctrl+Shift+T`, `F12`, etc.) are active without any configuration.

### Default `config.yaml` Template

```yaml
# Nitty Configuration — generated automatically on first launch
# Edit this file and save; changes hot-reload without restart.

[terminal]
scrollback_lines = 10000
bell = "visual"
cursor_style = "block"
cursor_blink = true
font_family = "Monospace"
font_size = 13

[appearance]
theme = "dracula"
opacity = 1.0

[hotkeys]
# Override defaults here, e.g.:
# new_tab = "ctrl+shift+t"

[quake]
enabled = false
hotkey = "F12"
height_percent = 40
monitor = "primary"
```

### Test Matrix

| Distribution | Package Format | First-Launch OK | Notes |
|---|---|---|---|
| Ubuntu 24.04 | `.deb` | ⬜ | |
| Ubuntu 22.04 | `.deb` | ⬜ | |
| Fedora 40 | `.rpm` | ⬜ | |
| Arch Linux | `.AppImage` | ⬜ | |
| (any) | Flatpak | ⬜ | |

---

## Upgrade Path Testing

### Expected: Upgrade from Prior Version

When upgrading from an older installed version (e.g., v0.10.5):

1. **Existing config preserved:** `~/.config/nitty/config.yaml` is not overwritten. Nitty parses the old file and fills in new keys with defaults.
2. **New config keys:** Any keys not present in the old config are silently added with their default values on the next write (e.g., when the user saves from the Settings UI).
3. **Connection profiles preserved:** `~/.config/nitty/profiles/*.toml` files are not touched by the package upgrade. All existing SSH/Serial/Telnet profiles remain accessible.
4. **No data loss:** The credential vault (`~/.config/nitty/vault.db`) is preserved across upgrades.

### Upgrade Test Steps

```bash
# 1. Install old version
sudo dpkg -i nitty_0.10.5_amd64.deb
# 2. Launch, create SSH profile "test-server", save config changes
# 3. Quit Nitty
# 4. Upgrade
sudo dpkg -i nitty_0.99.0-beta.1_amd64.deb
# 5. Launch and verify:
#   - "test-server" profile still appears in Connection Manager
#   - config.yaml still contains user's font/theme preferences
#   - New keys (e.g., any v0.99 additions) are present with defaults
```

### Upgrade Test Matrix

| From Version | To Version | Profiles OK | Config OK | Notes |
|---|---|---|---|---|
| v0.10.5 | v0.99.0-beta.1 | ⬜ | ⬜ | |
