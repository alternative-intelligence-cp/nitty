# Changelog

All notable changes to Nitty are documented in this file.

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versions follow [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [0.15.0] — 2026-06-19

### Added
- **Plugin Manager — Install from Directory**: File-chooser dialog (GTK4 native) for installing a plugin from a local directory. Validates that the selected path contains a `manifest.npk` before copying to `~/.local/share/nitty/plugins/`.
- **Plugin Manager — Settings Editor**: Full modal settings dialog (`nitty_gtk4_plugin_settings_dialog`) with one labeled entry per registered setting key. Changes are persisted immediately via `cfg_set_plugin_setting` + `config_save_plugin_settings`.
- **Plugin Manager — Real Uninstall**: `_pmu_do_uninstall` now calls `fs_rmdir_recursive` to fully delete the plugin directory; previously only disabled the plugin in-memory.
- **Config API**: `cfg_set_plugin_setting(plugin_name, key, value)` and `config_save_plugin_settings()` — write-side of the per-plugin settings store.
- **GTK4 Shim** (5 new functions): `nitty_gtk4_entry_set_text`, `nitty_gtk4_file_chooser_open`, `nitty_gtk4_plugin_settings_dialog`, `nitty_gtk4_plugin_settings_get_value`, `nitty_sys_exec`.
- **Test suite**: `tests/config/test_config_plugin_settings.npk` — 15 unit tests covering settings read/write round-trips, multi-plugin key isolation, and config file persistence.

### Fixed
- `tests/ssh/test_ssh_e2e.npk`: Renamed reserved keyword variable `ok` → `unlock_ok` (compiler regression fix).

---

## [0.14.2] — 2026-06-18

### Added
- **Beta test checklist** (`tests/beta/BETA_CHECKLIST.md`): 90-item structured checklist across 12 subsystems (Installation, Local Terminal, Tabs, Split Panes, SSH, Serial, Telnet, Configuration, Plugins, Search, Accessibility, Upgrade Path).
- **Fresh install notes** (`tests/beta/FRESH_INSTALL_NOTES.md`): First-launch behavior documentation, 5-row install test matrix, and step-by-step upgrade path from v0.10.5.
- **Application screenshots** (`screenshots/`): Four representative application renders for documentation — main window with split panes + SSH, connection manager, settings panel, serial hexdump mode.
- **KNOWN_ISSUES.md**: 10 triaged issues (5 medium, 5 low priority) with symptoms, workarounds, and target fix versions.
- **README rewrite**: Complete overhaul — accurate feature tables, package installation commands, keyboard shortcut reference, CI badge, screenshot grid, related projects index.

---

## [0.14.1] — 2026-06-17

### Added
- **CI workflow** (`.github/workflows/ci.yml`): Multi-distro build + test matrix (Ubuntu 24.04, Ubuntu 22.04); runs on push to `main` and all `dev-*` branches.
- **PR checks workflow** (`.github/workflows/pr-checks.yml`): Validates PR description format and commit message conventions.
- **Benchmark workflow** (`.github/workflows/bench.yml`): Performance regression detection on pushes to `main`.
- **Compatibility workflow** (`.github/workflows/compat.yml`): Weekly compatibility and stress tests (Sundays).
- **Automated release workflow** (`.github/workflows/release.yml`): Builds .deb, .rpm, AppImage, Flatpak packages and creates a GitHub Release on `v*.*.*` tag push.

---

## [0.14.0] — 2026-06-16

### Added
- **Debian package** (`packaging/deb/`): Complete `control`, `rules`, `changelog`, `copyright`, and `install` files. Build script `scripts/build-deb.sh`.
- **RPM package** (`packaging/rpm/`): `nitty.spec` with `%changelog` section. Build script `scripts/build-rpm.sh`.
- **AppImage** (`packaging/appimage/`): `AppImageBuilder.yml` + `linuxdeploy` integration. Build script `scripts/build-appimage.sh`.
- **Flatpak** (`packaging/flatpak/`): `com.nitty.Terminal.yaml` manifest. Build script `scripts/build-flatpak.sh`.
- **Desktop integration**: `packaging/nitty.desktop` (XDG entry), icon set (8 sizes from 16×16 to 512×512), `packaging/nitty.metainfo.xml` (AppStream metadata with screenshots and release history).
- **Release notes generator** (`scripts/generate-release-notes.sh`): Extracts per-version sections from `CHANGELOG.md` for automated GitHub Release bodies.

---

## [0.13.2] — 2026-06-15

### Added
- **Architecture overview** (`docs/architecture.md`): Complete module reference documenting the layered architecture — VT parser → renderer → GTK4 shim → event loop. Covers all subsystems: PTY, SSH, Serial, Telnet, Plugin, Config.
- **Module reference**: Detailed documentation for every source file in `src/` including pub func signatures, state ownership, and inter-module dependency graph.

---

## [0.13.1] — 2026-06-15

### Added
- **User manual** (`docs/user-manual.md`): End-user documentation covering installation, first launch, connection setup (SSH/Serial/Telnet), tab and pane management, plugin installation, and configuration reference.
- **Man page** (`packaging/nitty.1`): POSIX man page for `nitty(1)` with command-line flags, configuration paths, and environment variables.

---

## [0.13.0] — 2026-06-14

### Added
- **AT-SPI2 accessibility** (`shim/atspi/`): Screen reader integration via the AT-SPI2 D-Bus protocol. Terminal text is exposed as an `AtspiText` interface, enabling Orca and other AT clients to read terminal output.
- **High-contrast themes**: Two built-in HC themes (HC Light, HC Dark) that pass WCAG 2.1 AA contrast ratios for all foreground/background color pairs.
- **Keyboard navigation**: Full keyboard-only workflow — Tab/Shift+Tab cycles focus through toolbar, tab bar, and terminal widget; all menus and dialogs accessible without mouse.
- **Accessibility settings**: New `[accessibility]` config section — `screen_reader_mode`, `high_contrast`, `reduce_motion`, `focus_indicator_width`.
- **Focus indicator**: Configurable high-visibility focus ring (default 2px) drawn around the active terminal widget.
- **A11y test suite** (`tests/a11y/`): 22 tests covering AT-SPI object exposure, focus management, color contrast ratios, and keyboard navigation sequences.

---

## [0.12.0] — 2026-06-13 *(internal — not tagged separately)*

### Added
- **Rendering optimization**: Tile-based dirty tracking reduces per-frame work from O(rows×cols) to O(dirty_tiles). Achieves ~40% frame-time reduction on 200-column terminals.
- **GPU glyph cache**: Cairo surface cache for the 256 most-used Unicode codepoints. Cache hit avoids re-layout and re-render per frame.
- **Benchmark suite** (`tests/bench/`): 6 micro-benchmarks measuring renderer throughput, VT parser throughput, scrollback push/pop, config parse latency, plugin dispatch overhead, and SSH read latency.
- **Stress tests** (`tests/stress/`): 5 suites — terminal flood, rapid tab open/close, split pane exhaustion, config hot-reload under load, plugin dispatch storm.

---

## [0.11.0] — 2026-06-12

### Added
- **End-to-end test framework** (`tests/e2e/`): 5 test suites covering SSH vault, X11 forwarding init, Zmodem state machine, serial port state, and config round-trips.
- **Compatibility test suite** (`tests/compat/`): 180 tests covering VT100/VT220/xterm escape sequence compatibility, UTF-8 + combining mark rendering, 256-color and true-color output, Powerline/Nerd Font glyph widths.
- **Stress test suites** (`tests/stress/`): 329 total tests across resource exhaustion, concurrent I/O, and rapid state transitions.
- **UTF-8 + combining mark fixes**: Resolved 3 combining mark rendering regressions found during compat testing (zero-width joiner sequences, Devanagari, Arabic).
- **CI integration**: All test suites added to `scripts/run-tests.sh` with exit-code propagation.

---

## [0.10.5] — 2026-06-11

### Added
- **Plugin documentation index** (`docs/plugins/README.md`): Structured entry point for plugin developers, linking all plugin documentation.
- **Plugin development guide** (`docs/plugin-development-guide.md`): Comprehensive guide for plugin authors — lifecycle, extension points, settings API, event bus, testing, packaging.
- **Plugin API reference** (`docs/plugin-api-reference.md`): Complete function reference for all plugin API symbols.
- **Plugin manifest specification** (`docs/plugin-manifest.md`): `plugin.yaml` format reference with all fields, types, and constraints.
- **Word-highlighter example plugin** (`examples/plugins/word-highlighter/`): Fully functional example plugin demonstrating TerminalDecorator + settings API.
- **Plugin scaffolding script** (`scripts/new-plugin.sh`): Creates a new plugin project skeleton from a template with correct manifest, source layout, and test structure.
- **Plugin test utilities** (`tests/plugin-test-utils/`): Shared helpers for plugin unit testing — mock API, mock event bus, assertion helpers.
- **Example plugin tests**: 12 unit tests for the word-highlighter example covering all extension points.

---

*For changes before v0.11.0, see the project history in the git log or the META/NITTY planning directory.*
