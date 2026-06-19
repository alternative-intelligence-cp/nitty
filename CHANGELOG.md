# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.13.1] - 2026-06-18
### Added
- Comprehensive user manual (`docs/user-manual/`).
- Unix man page (`docs/nitty.1`).
- Printable keyboard shortcut quick-reference card.

## [0.13.0] - 2026-06-18
### Added
- Complete Accessibility (a11y) support via AT-SPI2 and Orca.
- `NittyTerminalAccessible` GTK widget wrapper for screen readers.
- High-Contrast Dark and Light built-in themes (strict WCAG AAA compliance).
- Keyboard skip-navigation (`Ctrl+F6`).
- Automatic detection of GTK system high-contrast preference.
- Live-region announcements for tab switching and screen clears.

## [0.12.0] - 2026-06-16
### Added
- Performance benchmarking framework (`test_throughput.npk`, `test_latency.npk`).
- Support for intercepting Zmodem (`sz`/`rz`) transfers natively.

### Fixed
- Segmentation fault during GTK4 object initialization due to early attachment.
- Compiler bug causing `nitpickc -c` to crash on unused variables.

## [0.11.2] - 2026-06-14
### Added
- Comprehensive compatibility and stress test suites (over 300 automated tests).
- E2E testing framework.

## [0.10.5] - 2026-06-12
### Added
- Plugin API and architecture (`PluginManager`, `PluginAPI`, Extension points).
- Plugin Safe Path sandbox to prevent rogue filesystem access.
- ConnectionProvider extension points (refactored SSH/Serial/Telnet).

## [0.9.4] - 2026-06-10
### Added
- Connection Profiles (SSH, Serial, Telnet).
- Serial Port support (Modes, History, Hexdump).
- Telnet Client (RFC 854 IAC negotiation).
- Built-in SFTP Browser panel.
- SSH Port forwarding and ProxyJump (jump host relay) support.

## [0.8.0] - 2026-06-05
### Added
- Full SSH Client integration (LibSSH2).
- Encrypted Credential Vault.
- Authentication dialogs and Keyboard-Interactive handling.

## [0.7.0] - 2026-06-01
### Added
- Quake Mode (drop-down terminal via global hotkey).
- Split Pane view (BSP tree layout).
- Terminal Search bar and hyperlink detection.
- Scrollback buffer implementation.
- Copy on select, and right-click paste capabilities.
- Visual and desktop notification bells.

## [0.6.0] - 2026-05-20
### Added
- Initial working release of the GTK4 GUI.
- VT100 parser core.
- Dynamic Configuration system (hot-reloading via inotify).
- Multi-chord hotkey engine.
