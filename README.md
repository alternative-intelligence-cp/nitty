# Nitty

**A full-featured, native terminal emulator written in [Nitpick](https://github.com/alternative-intelligence-cp/nitpick)**

[![License: AGPL v3](https://img.shields.io/badge/License-AGPL_v3-blue.svg)](https://www.gnu.org/licenses/agpl-3.0)

---

## Overview

Nitty is a modern terminal emulator built from scratch in the Nitpick programming language. It aims for feature parity with [Tabby](https://tabby.sh) while using a native GUI framework instead of Electron — delivering fast startup, low memory usage, and a truly native desktop experience.

Nitty is both a serious terminal emulator and a showcase for the Nitpick language ecosystem, demonstrating that complex desktop applications can be built entirely with Nitpick and its standard packages.

## Features (Planned)

### Core Terminal
- Full VT100/VT220/xterm escape sequence support
- 24-bit true color rendering with 150+ built-in color schemes
- GPU-accelerated text rendering
- Unicode, emoji, font ligatures, and Powerline/Nerd Font support
- Configurable scrollback buffer with search
- Bracketed paste, copy-on-select, multi-line paste warnings

### Window Management
- Tabbed terminal sessions with drag-and-drop reordering
- Horizontal and vertical split panes with nested layouts
- Broadcast input to all panes simultaneously
- Session persistence across restarts
- Quake-mode drop-down terminal

### Connections
- **Local Shell** — bash, zsh, fish, and any system shell
- **SSH** — Built-in SSH2 client (via libssh2) with connection manager, jump hosts, port forwarding, SFTP
- **Serial** — Full serial terminal with configurable baud rates and hex dump mode
- **Telnet** — Native Telnet client

### Configuration
- YAML-based configuration with hot-reload
- Graphical settings editor
- Per-connection-type profiles
- Fully customizable hotkeys with multi-chord support
- Themeable UI with built-in and custom themes

### Plugin System
- Extensible architecture with defined extension points
- First-party feature plugins (SSH, serial, telnet)
- Third-party plugin development support
- Plugin manager for discovery and installation

## Architecture

```
┌──────────────────────────────────────────────────────┐
│                   Nitty Application                   │
├───────────────┬─────────────────┬────────────────────┤
│ Plugin System │  Config Engine  │  Hotkey Manager     │
├───────────────┼─────────────────┼────────────────────┤
│ Tab Manager   │  Split Layout   │  Session Manager    │
├───────────────┼─────────────────┼────────────────────┤
│ Terminal Core │  VT Parser      │  Scrollback Buffer  │
├───────────────┼─────────────────┼────────────────────┤
│ PTY Layer     │  SSH Client     │  Serial Driver      │
├───────────────┴─────────────────┴────────────────────┤
│       GUI Renderer (GTK4 via FFI) + GPU Text Engine         │
├──────────────────────────────────────────────────────┤
│  nitpick-libc (termios, ioctl, fork, pipes, epoll)   │
└──────────────────────────────────────────────────────┘
```

## Building

### Prerequisites

- **Nitpick compiler** (`npkc`) v0.52.15 or later
- **Nitpick build system** (`npkbld`)
- **nitpick-libc** v0.3.0 or later
- **nitpick-packages** (ecosystem packages)
- GUI framework development libraries (TBD)
- LLVM 20 (required by npkc)

### Build from Source

```bash
git clone https://github.com/alternative-intelligence-cp/nitty.git
cd nitty
npkbld build
```

### Run

```bash
./build/nitty
```

### Run Tests

```bash
npkbld test
```

## Project Status

**Status: Pre-Development (Planning Phase)**

Nitty is currently in the planning phase. The project structure, roadmap, and feature set have been defined. Development will begin with GUI framework evaluation and project scaffolding.

See the [Master Roadmap](https://github.com/alternative-intelligence-cp/nitty/wiki) for detailed release plans.

## Related Projects

| Project | Description |
|---------|-------------|
| [nitpick](https://github.com/alternative-intelligence-cp/nitpick) | The Nitpick programming language compiler |
| [nitpick-build](https://github.com/alternative-intelligence-cp/nitpick-build) | Build system for Nitpick projects |
| [nitpick-packages](https://github.com/alternative-intelligence-cp/nitpick-packages) | 105+ standard library packages |
| [nitpick-libc](https://github.com/alternative-intelligence-cp/nitpick-libc) | Pure Nitpick libc implementation |
| [nitpick-docs](https://github.com/alternative-intelligence-cp/nitpick-docs) | Language documentation and guides |
| [nikos](https://github.com/alternative-intelligence-cp/nikos) | Static analyzer (NASA IKOS fork) |

## Contributing

Nitty is developed by the Alternative Intelligence CP team. Contributions are welcome once the project reaches a stable foundation.

Please see `CONTRIBUTING.md` (coming soon) for guidelines.

## License

This project is licensed under the **GNU Affero General Public License v3.0** — see the [LICENSE](LICENSE) file for details.

## Acknowledgments

- **[Tabby](https://tabby.sh)** by Eugene Pankov — The feature reference and inspiration for Nitty
- **[Nitpick](https://github.com/alternative-intelligence-cp/nitpick)** — The programming language that makes this possible
