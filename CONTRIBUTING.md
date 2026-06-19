# Contributing to Nitty

Thank you for your interest in contributing to Nitty — a full-featured, native terminal emulator written in [Nitpick](https://github.com/alternative-intelligence-cp/nitpick).

---

## Table of Contents

- [Getting Started](#getting-started)
- [Development Environment](#development-environment)
- [Project Structure](#project-structure)
- [Coding Guidelines](#coding-guidelines)
- [Testing](#testing)
- [Submitting Changes](#submitting-changes)
- [Reporting Issues](#reporting-issues)

---

## Getting Started

1. **Fork** the repository on GitHub
2. **Clone** your fork locally:
   ```bash
   git clone https://github.com/YOUR_USERNAME/nitty.git
   cd nitty
   ```
3. **Set up the development environment** (see below)
4. **Create a feature branch** from `main`:
   ```bash
   git checkout -b feature/my-feature main
   ```
5. **Make your changes**, add tests, and commit
6. **Open a Pull Request** against `main`

---

## Development Environment

### Prerequisites

| Dependency | Version | Purpose |
|---|---|---|
| [Nitpick compiler](https://github.com/alternative-intelligence-cp/nitpick) (`npkc`) | ≥ 0.52.15 | Compiles `.npk` source files |
| [Nitpick build system](https://github.com/alternative-intelligence-cp/nitpick-build) (`npkbld`) | latest | Builds the project |
| LLVM | 20 | Backend for `npkc` |
| GTK4 dev headers | ≥ 4.6 | `libgtk-4-dev` |
| libssh2 dev headers | ≥ 1.10 | `libssh2-1-dev` |
| OpenSSL dev headers | ≥ 3.0 | `libssl-dev` |

### Build

```bash
# Build all C shims (one-time setup)
make -C shim/gtk4/
make -C shim/libssh2/
make -C shim/pty/
make -C shim/serial/
make -C shim/telnet/

# Build Nitty
npkbld build

# Run the application
./build/nitty
```

### Run Tests

```bash
# Unit and integration tests
npkbld test tests/

# End-to-end tests (advisory — requires display)
bash tests/e2e/run_e2e.sh

# Compatibility suite
npkbld test tests/compat/
```

---

## Project Structure

```
nitty/
├── src/
│   ├── core/        # App lifecycle, pane manager, event loop
│   ├── terminal/    # VT100/xterm escape parser, state machine, renderer
│   ├── gui/         # GTK4 widgets (terminal widget, tab bar, dialogs)
│   ├── config/      # YAML/TOML config, per-connection profiles
│   ├── ssh/         # libssh2-backed SSH client, vault, X11, Zmodem
│   ├── serial/      # Serial port client (termios), baud rates, modes
│   ├── telnet/      # RFC 854 Telnet client with IAC negotiation
│   └── plugin/      # Plugin loader, extension point registry, event bus
├── shim/            # C shims bridging Nitpick ↔ C libraries
│   ├── gtk4/        # GTK4 widget shim (rendering, input, dialogs)
│   ├── libssh2/     # libssh2 session/channel shim
│   ├── pty/         # POSIX PTY (openpty/forkpty) shim
│   ├── serial/      # POSIX serial port (termios) shim
│   └── telnet/      # TCP socket shim for Telnet
├── tests/           # All test suites
│   ├── e2e/         # End-to-end tests
│   ├── compat/      # VT compatibility tests
│   ├── ssh/         # SSH feature tests
│   ├── serial/      # Serial feature tests
│   ├── config/      # Config system tests
│   ├── plugin/      # Plugin API tests
│   ├── stress/      # Resource stress tests
│   └── beta/        # Manual beta test checklist
├── docs/            # Developer documentation
├── packaging/       # Distribution packaging (deb, rpm, AppImage, Flatpak)
├── scripts/         # Build and release helper scripts
├── screenshots/     # Application screenshots for documentation
└── build.abc        # Nitpick build manifest
```

---

## Coding Guidelines

### Nitpick Style

- **`pub func`** declarations use `pass(value)` to return a value and `exit(code)` to terminate a program entry point. Use `raw func_name(args)` to call a function that returns a Result type and discard the error variant.
- **Narrowing casts** require an explicit `@cast_unchecked<int32>(expr)` annotation.
- **No reserved keywords as identifiers** — `ok`, `err`, `pass`, `fail`, `drop`, `raw`, `exit` are reserved.
- **Flat arrays** are preferred over generic container types (`List<T>`) for hot paths — use parallel int64 arrays with an explicit count variable.
- **Naming**: `snake_case` for all identifiers. Module-level globals prefixed with `g_`. Constants use `UPPER_SNAKE`.

### C Shim Style

- All function signatures use `int64_t` for opaque handles and boolean results; `const char *` for strings.
- Include `stdint.h` for `int64_t`. Do not use `long` or `int` for values crossing the FFI boundary.
- Prefix all exported symbols with `nitty_` to avoid linker collisions.
- Static buffers use `[16384]` bytes for read buffers (consistent with PTY shim).

### General

- Keep functions focused and under ~80 lines where practical.
- Preserve all existing comments — do not remove documentation comments without cause.
- Prefer clarity over cleverness. This codebase is a Nitpick language showcase.

---

## Testing

All contributions should include tests. The test structure mirrors `src/`:

- `tests/config/` for config system changes
- `tests/ssh/` for SSH subsystem changes
- `tests/serial/` for serial subsystem changes
- `tests/plugin/` for plugin API changes

New test files should be registered in `build.abc` as a binary target.

### Test Format

Tests use the project's lightweight pass/fail harness (see any existing test file for the `pass_test` / `fail_test` / `assert_eq_i64` / `assert_eq_str` pattern). Tests return exit code 0 on all-pass, 1 on any failure.

---

## Submitting Changes

### Commit Messages

Follow [Conventional Commits](https://www.conventionalcommits.org/) style:

```
feat: add serial port DTR/RTS control via toolbar buttons
fix: correct vault unlock variable name (reserved keyword 'ok')
docs: update plugin settings API reference for v0.15.0
test: add 15 unit tests for cfg_set_plugin_setting round-trip
chore: register test_config_plugin_settings in build.abc
```

### Pull Request Checklist

- [ ] Changes are covered by tests
- [ ] `npkbld build` succeeds (same failure count as before your change)
- [ ] Code follows the Nitpick and C shim style guides above
- [ ] Documentation updated if adding/changing a public API
- [ ] `CHANGELOG.md` updated with a summary of your changes under `[Unreleased]`

---

## Reporting Issues

Please open a GitHub Issue with:

1. **Nitty version** (`nitty --version` or the latest git tag)
2. **Operating system and distribution** (e.g., Ubuntu 24.04, Fedora 40)
3. **Installation method** (.deb, .rpm, AppImage, Flatpak, or built from source)
4. **Steps to reproduce** the issue
5. **Expected behavior** vs. **actual behavior**
6. Any relevant error output or log lines

For security vulnerabilities, please do **not** open a public issue. Contact the maintainers privately via GitHub's private vulnerability reporting feature.

---

## License

By contributing to Nitty, you agree that your contributions will be licensed under the [GNU Affero General Public License v3.0](LICENSE).
