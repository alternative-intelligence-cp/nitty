# Nitty User Manual

Welcome to the Nitty User Manual. Nitty is a modern, fast, and feature-rich terminal emulator written in the Nitpick programming language, built from the ground up for power users. It combines standard local shell capabilities with advanced features like integrated SSH connection management, a built-in encrypted credential vault, SFTP browsing, native serial port and telnet support, and a flexible plugin system.

## System Requirements

- **Operating System:** Linux (X11 or Wayland)
- **Dependencies:** GTK4, AT-SPI2 (for accessibility), OpenSSL (for SSH and Vault)
- **Terminal Features:** 24-bit true color, Unicode 15 support, Ligatures (via Pango)

## Installation

### From Source

Ensure you have the Nitpick compiler (`nitpickc`) and build tool (`npkbld`) installed, along with development headers for GTK4 and OpenSSL.

```bash
# Clone the repository
git clone https://github.com/altintel/nitty.git
cd nitty

# Build using npkbld
npkbld build

# The binary will be located in .nitpick_make/build/nitty
./.nitpick_make/build/nitty
```

*(Note: Pre-built AppImages and distribution packages are planned for a future release.)*

## First Launch

When you first launch Nitty, you'll see a clean, modern terminal interface. By default, it opens your system's default shell (usually `bash` or `zsh`). 

The main interface components are:
- **Tab Bar:** At the top, showing your open sessions.
- **Terminal Area:** The main interaction area.
- **Connection Manager:** (Hidden by default) Press `Ctrl+Shift+B` to open the sidebar for managing SSH profiles and Vault credentials.
- **Sidebar Panes:** Nitty supports a plugin manager and SFTP browser, which also live in the sidebar.

## Manual Structure

This manual is divided into several guides to help you get the most out of Nitty:

1. **[Getting Started](getting-started.md)**: Basic usage, tabs, splits, and quake mode.
2. **[Configuration Guide](configuration.md)**: Customizing Nitty via `config.yaml`, profiles, and themes.
3. **[Keyboard Shortcuts](hotkeys.md)**: Complete reference of all default hotkeys and how to customize them.
4. **[SSH Guide](ssh.md)**: Managing connections, authentication, port forwarding, and SFTP.
5. **[Serial & Telnet Guide](serial.md)**: Connecting to embedded devices and serial consoles.
6. **[Plugin Guide](plugins.md)**: Extending Nitty with custom plugins.

For a quick summary of all default keyboard shortcuts, see the [Quick Reference Card](../quick-reference-card.md).
