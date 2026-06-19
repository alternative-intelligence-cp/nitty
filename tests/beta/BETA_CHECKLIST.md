# Nitty v0.99.0-beta.1 — Beta Test Checklist

Use this checklist to manually verify every Nitty feature before the v1.0 release.
Mark each item `[x]` for pass, `[!]` for fail, or `[-]` for not applicable.
Add notes in the **Issues** column for anything requiring a fix.

| Status | Version | Date | Tester |
|--------|---------|------|--------|
| ⬜ In Progress | 0.99.0-beta.1 | | |

---

## 1. Installation & First Launch

| # | Test | Result | Issues |
|---|------|--------|--------|
| 1.1 | Fresh install from `.deb` on Ubuntu 24.04 — package installs cleanly | `[ ]` | |
| 1.2 | Fresh install from `.rpm` on Fedora 40 — package installs cleanly | `[ ]` | |
| 1.3 | AppImage runs on Ubuntu 22.04 with no pre-installed dependencies | `[ ]` | |
| 1.4 | Flatpak installs and launches from sandbox correctly | `[ ]` | |
| 1.5 | First launch: `~/.config/nitty/` directory created automatically | `[ ]` | |
| 1.6 | First launch: `config.yaml` generated with sensible defaults | `[ ]` | |
| 1.7 | First launch: Default theme applied correctly | `[ ]` | |
| 1.8 | First launch: System shell detected and opened correctly | `[ ]` | |
| 1.9 | First launch: No error dialogs or warnings shown | `[ ]` | |
| 1.10 | Application icon appears in system app menu | `[ ]` | |
| 1.11 | `man nitty` accessible after package install | `[ ]` | |
| 1.12 | Uninstall via package manager removes all files cleanly | `[ ]` | |

---

## 2. Local Terminal

| # | Test | Result | Issues |
|---|------|--------|--------|
| 2.1 | Bash shell opens and is interactive | `[ ]` | |
| 2.2 | 24-bit true color rendering — `printf '\e[38;2;255;0;128mCOLOR\e[0m'` | `[ ]` | |
| 2.3 | 256-color rendering — `for i in {0..255}; do printf "\e[38;5;${i}m  $i"; done` | `[ ]` | |
| 2.4 | ANSI bold, italic, underline, strikethrough rendering | `[ ]` | |
| 2.5 | Scrollback buffer works — fill screen and scroll up | `[ ]` | |
| 2.6 | Window resize: terminal grid resizes correctly, no garbling | `[ ]` | |
| 2.7 | Block, underline, bar cursor styles (configured in `config.yaml`) | `[ ]` | |
| 2.8 | Cursor blink toggle works | `[ ]` | |
| 2.9 | Copy with `Ctrl+Shift+C` and paste with `Ctrl+Shift+V` | `[ ]` | |
| 2.10 | Mouse selection selects text correctly | `[ ]` | |
| 2.11 | Multi-line paste warning dialog appears | `[ ]` | |
| 2.12 | Right-click context menu appears | `[ ]` | |
| 2.13 | URL click opens browser | `[ ]` | |
| 2.14 | Bell: audible, visual, desktop notification modes | `[ ]` | |
| 2.15 | Screen clear (`Ctrl+L` or `Ctrl+Shift+K`) | `[ ]` | |
| 2.16 | Unicode: emoji, CJK characters, combining marks render correctly | `[ ]` | |
| 2.17 | Bracketed paste mode activated by apps (e.g., vim) | `[ ]` | |

---

## 3. Tab Management

| # | Test | Result | Issues |
|---|------|--------|--------|
| 3.1 | New tab opens with `Ctrl+Shift+T` | `[ ]` | |
| 3.2 | Close tab with `Ctrl+Shift+W` | `[ ]` | |
| 3.3 | Switch tabs with `Ctrl+Tab` and `Ctrl+Shift+Tab` | `[ ]` | |
| 3.4 | Switch tabs directly with `Alt+1` through `Alt+9` | `[ ]` | |
| 3.5 | Tab title shows running process name | `[ ]` | |
| 3.6 | Activity indicator on background tab when output occurs | `[ ]` | |
| 3.7 | Process completion notification for background tab | `[ ]` | |
| 3.8 | Confirm close dialog when multiple tabs open | `[ ]` | |

---

## 4. Split Panes

| # | Test | Result | Issues |
|---|------|--------|--------|
| 4.1 | Horizontal split with `Ctrl+Shift+H` | `[ ]` | |
| 4.2 | Vertical split with `Ctrl+Shift+V` | `[ ]` | |
| 4.3 | Close pane with `Ctrl+Shift+X` | `[ ]` | |
| 4.4 | Navigate panes with `Alt+Arrow` keys | `[ ]` | |
| 4.5 | Nested splits (split a split) work correctly | `[ ]` | |
| 4.6 | Pane resize by dragging the divider | `[ ]` | |

---

## 5. SSH

| # | Test | Result | Issues |
|---|------|--------|--------|
| 5.1 | Quick connect via Connection Manager `user@host` | `[ ]` | |
| 5.2 | Password authentication | `[ ]` | |
| 5.3 | Public key authentication (`~/.ssh/id_ed25519`) | `[ ]` | |
| 5.4 | SSH Agent authentication | `[ ]` | |
| 5.5 | Keyboard-interactive 2FA prompt | `[ ]` | |
| 5.6 | TOFU host key acceptance on first connect | `[ ]` | |
| 5.7 | Known hosts mismatch error displayed correctly | `[ ]` | |
| 5.8 | Credential Vault: save password, reconnect without prompt | `[ ]` | |
| 5.9 | SSH profile: connect from saved profile | `[ ]` | |
| 5.10 | ProxyJump / Jump Host connection | `[ ]` | |
| 5.11 | Local port forwarding (`-L`) | `[ ]` | |
| 5.12 | Remote port forwarding (`-R`) | `[ ]` | |
| 5.13 | SFTP browser: open panel, navigate, download a file | `[ ]` | |
| 5.14 | SFTP browser: upload a file via drag-and-drop | `[ ]` | |
| 5.15 | X11 Forwarding: a GUI app (e.g., `xeyes`) displays locally | `[ ]` | |
| 5.16 | Import from `~/.ssh/config`: profiles appear in Connection Manager | `[ ]` | |

---

## 6. Serial

| # | Test | Result | Issues |
|---|------|--------|--------|
| 6.1 | Port enumeration: `/dev/ttyUSB*` devices listed correctly | `[ ]` | |
| 6.2 | Connect at 115200 8N1 | `[ ]` | |
| 6.3 | Connect at 9600 8N1 | `[ ]` | |
| 6.4 | Text mode: console output renders correctly | `[ ]` | |
| 6.5 | Hexdump mode toggle with `F8` | `[ ]` | |
| 6.6 | Line mode: text sent only on Enter | `[ ]` | |
| 6.7 | Toggle DTR via context menu | `[ ]` | |
| 6.8 | Send BREAK with `Ctrl+Shift+\`` | `[ ]` | |
| 6.9 | Zmodem auto-detect: `sz` on remote triggers download dialog | `[ ]` | |

---

## 7. Telnet

| # | Test | Result | Issues |
|---|------|--------|--------|
| 7.1 | Connect to a Telnet server on port 23 | `[ ]` | |
| 7.2 | Telnet IAC negotiation (Terminal Type, Echo, SGA) handled silently | `[ ]` | |

---

## 8. Configuration

| # | Test | Result | Issues |
|---|------|--------|--------|
| 8.1 | `config.yaml` changes hot-reload without restart | `[ ]` | |
| 8.2 | Font family and size changes take effect | `[ ]` | |
| 8.3 | Opacity change takes effect | `[ ]` | |
| 8.4 | Theme change takes effect | `[ ]` | |
| 8.5 | Custom hotkey binding in `[hotkeys]` section works | `[ ]` | |
| 8.6 | Quake mode: enable in config, toggle with `F12` | `[ ]` | |
| 8.7 | Settings UI: open with `Ctrl+Shift+,` | `[ ]` | |
| 8.8 | Settings UI: save applies changes | `[ ]` | |

---

## 9. Plugins

| # | Test | Result | Issues |
|---|------|--------|--------|
| 9.1 | Plugin Manager opens with `Ctrl+Shift+P` | `[ ]` | |
| 9.2 | Install a user plugin (copy to `~/.config/nitty/plugins/`) | `[ ]` | |
| 9.3 | Enable and disable a plugin via toggle | `[ ]` | |
| 9.4 | Plugin settings dialog opens for a configurable plugin | `[ ]` | |
| 9.5 | Uninstall plugin: delete folder, disable in manager | `[ ]` | |

---

## 10. Search

| # | Test | Result | Issues |
|---|------|--------|--------|
| 10.1 | Search bar opens with `Ctrl+Shift+F` | `[ ]` | |
| 10.2 | Typing query highlights all matches in scrollback | `[ ]` | |
| 10.3 | Enter / Shift+Enter navigates between matches | `[ ]` | |
| 10.4 | Search closed, highlight cleared | `[ ]` | |

---

## 11. Accessibility

| # | Test | Result | Issues |
|---|------|--------|--------|
| 11.1 | Orca screen reader announces terminal output | `[ ]` | |
| 11.2 | All UI controls reachable by keyboard only (no mouse required) | `[ ]` | |
| 11.3 | High-Contrast Dark theme selectable and correct | `[ ]` | |
| 11.4 | `reduce_motion = 1` disables cursor blink and Quake animation | `[ ]` | |
| 11.5 | `Ctrl+F6` skip-navigation focuses the terminal | `[ ]` | |

---

## 12. Upgrade Path Testing

| # | Test | Result | Issues |
|---|------|--------|--------|
| 12.1 | Install prior version, create profiles and config | `[ ]` | |
| 12.2 | Upgrade to beta: existing `config.yaml` still parses correctly | `[ ]` | |
| 12.3 | New config keys added with correct defaults | `[ ]` | |
| 12.4 | Connection profiles preserved after upgrade | `[ ]` | |
