# Getting Started

This guide covers the day-to-day basics of using the Nitty terminal emulator.

## Opening a Local Terminal Session

When you start Nitty, it automatically opens a local terminal session using your system's default shell (e.g., `bash`, `zsh`). 

To open an additional local session, you can open a new tab (`Ctrl+Shift+T`) or split the current pane horizontally (`Ctrl+Shift+H`) or vertically (`Ctrl+Shift+V`).

## Basic Terminal Usage

Nitty provides modern terminal features to make your workflow efficient.

### Copy and Paste

- **Copy:** Select text with the mouse to automatically place it in the primary selection clipboard (if configured) or press `Ctrl+Shift+C` to copy to the system clipboard. Nitty also supports a `terminal.copy_on_select` setting in your configuration to automatically copy text to the system clipboard upon selection.
- **Paste:** Press `Ctrl+Shift+V` to paste from the system clipboard. If you are pasting multiple lines, Nitty will display a confirmation dialog by default to prevent accidental execution of commands.
- **Right-Click Paste:** You can enable right-click pasting via `terminal.right_click_paste` in your configuration.

### Search

Press `Ctrl+Shift+F` to open the search bar. The search bar allows you to quickly find text in the scrollback buffer. Type your query, and use the provided buttons (or `Enter`/`Shift+Enter`) to navigate through the results.

### Zoom

You can adjust the font size dynamically without changing your configuration file:
- **Zoom In:** `Ctrl+Shift+=`
- **Zoom Out:** `Ctrl+Shift+-`
- **Reset Zoom:** `Ctrl+Shift+0`

## Tab Management

Nitty supports a modern tabbed interface to keep multiple sessions organized.

- **New Tab:** `Ctrl+Shift+T`
- **Close Tab:** `Ctrl+Shift+W` (or click the 'X' on the tab)
- **Next Tab:** `Ctrl+Tab`
- **Previous Tab:** `Ctrl+Shift+Tab`
- **Switch to specific tab:** `Alt+1` through `Alt+9`

## Split Panes

Nitty allows you to split any tab into multiple resizable panes, giving you a customized workspace layout within a single window.

- **Split Horizontally (side-by-side):** `Ctrl+Shift+H`
- **Split Vertically (top-and-bottom):** `Ctrl+Shift+V`
- **Close Pane:** `Ctrl+Shift+X` (or type `exit` in the shell)
- **Navigate Panes:** `Alt+Left`, `Alt+Right`, `Alt+Up`, `Alt+Down`

## Quake Mode (Drop-Down Terminal)

Nitty features a "Quake Mode" that provides a persistent, drop-down terminal accessible from anywhere via a global hotkey.

To enable Quake mode, update your `~/.config/nitty/config.yaml` to include:

```yaml
[quake]
enabled = 1
hotkey = "F12"
position = "top"
width_percent = 100
height_percent = 50
animation_duration = 150
```

Once configured and restarted, pressing `F12` (or your chosen hotkey) will instantly toggle the visibility of the Nitty window, sliding it down from the top of the screen.

## Next Steps

Now that you know the basics, explore the [Configuration Guide](configuration.md) to customize Nitty to your liking, or read the [SSH Guide](ssh.md) to learn how to manage remote connections.
