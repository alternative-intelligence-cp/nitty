# docs/gtk4_ffi.md — GTK4 FFI Boundary Documentation

## Overview

Nitty uses a thin C wrapper library (`nitty_gtk4_shim`) to bridge Nitpick code
to the GTK4 widget toolkit. This document describes the FFI boundary design,
all shim functions, and the call patterns used on the Nitpick side.

## Why a C Shim?

GTK4's C API relies heavily on:
- GObject's type system (runtime class hierarchy via `G_TYPE_CHECK_INSTANCE_CAST`)
- Macro-based upcasting (`GTK_WINDOW()`, `GTK_APPLICATION()`, etc.)
- GLib signal registration with `g_signal_connect`
- Complex struct layouts not accessible from Nitpick's FFI

The shim encapsulates all of this, exposing a simplified interface where:
- All GTK object pointers are opaque `int64_t` handles
- All parameters are primitive types (int64, string)
- There are no macros, callbacks, or GObject types on the Nitpick side

## File Locations

| File | Purpose |
|---|---|
| `shim/gtk4/nitty_gtk4_shim.h` | C function declarations (public API) |
| `shim/gtk4/nitty_gtk4_shim.c` | C implementation |
| `src/gui/gtk4_ffi.npk` | Nitpick `extern` declarations (one per shim function) |
| `src/gui/window.npk` | High-level Nitpick window management (`pub func` wrappers) |

## v0.0.1 Architecture

In v0.0.1, the callback flow is simplified:

```
Nitpick                          C Shim                    GTK4
-------                          ------                    ----
window_configure(title, w, h) → nitty_gtk4_configure_window()  → stores config
window_app_create(app_id)     → nitty_gtk4_app_new()           → gtk_application_new()
                                 g_signal_connect("activate")   → on_activate registered
window_app_run(app)           → nitty_gtk4_app_run()           → g_application_run()
                                                                  [BLOCKS]
                                 on_activate() fires internally:
                                   gtk_application_window_new()
                                   gtk_window_set_title()
                                   gtk_window_set_default_size()
                                   gtk_window_present()
                                                                  [user closes window]
                              ← nitty_gtk4_app_run() returns     ←
window_app_free(app)          → nitty_gtk4_app_free()          → g_object_unref()
```

Full Nitpick callback dispatch (registering Nitpick functions as C callbacks)
is planned for v0.1.x.

## Shim Function Reference

### Pre-run Configuration

#### `nitty_gtk4_configure_window(title, width, height)`

Configure the main window before calling `app_run`. Must be called first.

| Parameter | C Type | Nitpick Type | Description |
|---|---|---|---|
| title | `const char*` | `string` | Window title (NULL = default "Nitty Terminal") |
| width | `int64_t` | `int64` | Width in pixels (0 = default 1024) |
| height | `int64_t` | `int64` | Height in pixels (0 = default 768) |

---

### Application Lifecycle

#### `nitty_gtk4_app_new(app_id) → int64`

Create a new GtkApplication.

| Parameter | C Type | Nitpick Type | Description |
|---|---|---|---|
| app_id | `const char*` | `string` | Application ID (e.g. "com.altintel.nitty") |

Returns: opaque app pointer (0 on failure)

#### `nitty_gtk4_app_run(app_ptr) → int64`

Run the GTK main loop. **Blocks** until the last window is closed.

Returns: application exit status (0 = success)

#### `nitty_gtk4_app_quit(app_ptr)`

Request the application to quit (non-blocking).

#### `nitty_gtk4_app_free(app_ptr)`

Release the GtkApplication reference. Call after `app_run` returns.

---

### Window Access

#### `nitty_gtk4_get_main_window() → int64`

Get the main window created by the internal activate handler. Returns 0 if
not yet created (i.e., before `app_run` fires the activate signal).

---

### Window Management

#### `nitty_gtk4_window_new(app_ptr) → int64`

Create a new GtkApplicationWindow (for advanced multi-window use).

#### `nitty_gtk4_window_set_title(win_ptr, title)`
#### `nitty_gtk4_window_set_size(win_ptr, width, height)`
#### `nitty_gtk4_window_show(win_ptr)`
#### `nitty_gtk4_window_close(win_ptr)`
#### `nitty_gtk4_window_get_width(win_ptr) → int64`
#### `nitty_gtk4_window_get_height(win_ptr) → int64`

Standard window operations. All safe to call with `win_ptr == 0` (no-ops).

---

## Nitpick FFI Call Patterns

### extern function calls (direct, no Result wrapping)

```
// extern functions return their declared type directly:
int64:app = nitty_gtk4_app_new("com.altintel.nitty");  // plain int64, no Result
nitty_gtk4_configure_window("Nitty", 1024i64, 768i64); // NIL return, no drop needed
```

### pub func calls (Result<T> wrapping applies)

```
// pub func from use'd module → Result<T>:
drop(window_configure("Nitty Terminal", 1024i64, 768i64));  // NIL → drop()
Result<int64>:app_r = window_app_create("com.altintel.nitty");
int64:app = app_r ? 0i64;                                   // unwrap with default
```

## Build Configuration

The shim is built as a static library (`libnitty_gtk4_shim.a`) by npkbld.
GTK4 include flags are inlined in `build.abc` (npkbld does not support
`pkg_config` yet). The Nitpick binary links against the shim + system GTK4 libs.

```ini
[target.gtk4_shim]
type = "c_library"
sources = ["shim/gtk4/nitty_gtk4_shim.c"]
compiler = "gcc"
flags = ["-fPIC", "-O2", "-Wall", "-Wextra", "-I/usr/include/gtk-4.0", ...]
output = "libnitty_gtk4_shim.a"

[target.nitty]
type = "binary"
deps = ["gtk4_shim"]
link_libraries = ["nitty_gtk4_shim", "gtk-4", "glib-2.0", ...]
link_paths = [".nitpick_make/build"]
```

## Static Analysis

The shim passes `gcc -fanalyzer -Wall -Wextra` with zero warnings in our code.
The one pedantic warning (`-Wpedantic`) is from GTK4's own headers, not ours.

## Future Work (v0.1.x)

- Full Nitpick callback dispatch: pass `(NIL)(int64)` function pointers to C
- Widget system: GtkBox, GtkLabel, GtkScrolledWindow, VTE terminal widget
- Per-widget signal registration via the callback table
