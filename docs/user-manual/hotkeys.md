# Keyboard Shortcuts

Nitty is designed to be fully navigable via the keyboard. Below is the complete reference of default keyboard shortcuts.

## Default Hotkeys

### Tab Management
| Action | Default Hotkey | Description |
|---|---|---|
| `tab.new` | **Ctrl+Shift+T** | Open a new local terminal tab |
| `tab.close` | **Ctrl+Shift+W** | Close the current tab |
| `tab.next` | **Ctrl+Tab** | Switch to the next tab to the right |
| `tab.prev` | **Ctrl+Shift+Tab** | Switch to the previous tab to the left |
| `tab.1` - `tab.9` | **Alt+1** - **Alt+9** | Switch directly to a specific tab by number |

### Pane Management (Splits)
| Action | Default Hotkey | Description |
|---|---|---|
| `pane.split_h` | **Ctrl+Shift+H** | Split the current pane horizontally (side-by-side) |
| `pane.split_v` | **Ctrl+Shift+V** | Split the current pane vertically (top-and-bottom) |
| `pane.close` | **Ctrl+Shift+X** | Close the currently focused pane |
| `pane.focus_next` | **Alt+Right** | Move focus to the pane on the right |
| `pane.focus_prev` | **Alt+Left** | Move focus to the pane on the left |
| `pane.focus_up` | **Alt+Up** | Move focus to the pane above |
| `pane.focus_down` | **Alt+Down** | Move focus to the pane below |

### Editing and Interaction
| Action | Default Hotkey | Description |
|---|---|---|
| `edit.copy` | **Ctrl+Shift+C** | Copy selected text to clipboard |
| `edit.paste` | **Ctrl+Shift+V** | Paste text from clipboard |
| `edit.search` | **Ctrl+Shift+F** | Open the search bar to find text in the scrollback |
| `edit.clear` | **Ctrl+Shift+K** | Clear the screen and the scrollback buffer |

### View and Appearance
| Action | Default Hotkey | Description |
|---|---|---|
| `view.fullscreen` | **F11** | Toggle fullscreen mode |
| `view.maximize` | **F10** | Toggle maximized window state |
| `view.always_on_top` | **Ctrl+Shift+A** | Toggle keeping the window on top of all others |
| `view.zoom_in` | **Ctrl+Shift+=** | Increase font size |
| `view.zoom_out` | **Ctrl+Shift+-** | Decrease font size |
| `view.zoom_reset` | **Ctrl+Shift+0** | Reset font size to configured default |

### Application & Tools
| Action | Default Hotkey | Description |
|---|---|---|
| `connection.manager` | **Ctrl+Shift+B** | Toggle the Connection Manager / Vault sidebar |
| `sftp.browser` | **Ctrl+Shift+S** | Toggle the SFTP file browser pane |
| `plugins.manager` | **Ctrl+Shift+P** | Toggle the Plugin Manager sidebar |
| `app.settings` | **Ctrl+Shift+,** | Open the settings dialog |
| `app.quit` | **Ctrl+Shift+Q** | Quit Nitty |
| `quake.toggle` | **F12** (Default) | Toggle Quake mode (drop-down terminal), if enabled |

### Accessibility & Specialized
| Action | Default Hotkey | Description |
|---|---|---|
| `a11y.skip_nav` | **Ctrl+F6** | Skip navigation: Focus the main terminal pane for Screen Readers |
| `serial.toggle_hex` | **F8** | Toggle Hexdump view for Serial connections |
| `serial.send_break` | **Ctrl+Shift+\`** | Send a BREAK signal over a Serial connection |

---

## Customizing Hotkeys

You can customize any shortcut in your `~/.config/nitty/config.yaml` file by adding a `[hotkeys]` section.

### Syntax

Map the internal action name (e.g., `tab.new`) to your desired key combination.

Modifiers supported: `Ctrl`, `Shift`, `Alt`, `Super` (Windows/Mac key). Connect them with `+`.

```yaml
[hotkeys]
tab.new = "Ctrl+Shift+N"
edit.copy = "Super+C"
edit.paste = "Super+V"
pane.split_h = "Ctrl+Alt+H"
```

### Disabling a Hotkey

To completely unbind a default shortcut, set its value to an empty string:

```yaml
[hotkeys]
app.quit = ""
```

### Multi-Chord Shortcuts

Nitty supports multi-chord shortcuts (sequences of keystrokes). Separate the chords with a space.

For example, to bind "Split Horizontally" to `Ctrl+K` followed by `H`:

```yaml
[hotkeys]
pane.split_h = "Ctrl+K H"
```

### Resolving Conflicts

If you map the same keystroke to multiple actions, Nitty resolves the conflict by keeping the last mapped definition. When launched from a terminal, Nitty will print a warning log indicating the conflict to help you debug your configuration.
