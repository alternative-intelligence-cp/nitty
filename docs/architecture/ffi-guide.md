# FFI & C Shim Guide

Nitpick cannot natively interface with complex C libraries (like GTK4 or LibSSH2) that rely heavily on macros, variadic functions, and complex C structs. To solve this, Nitty uses a **C Shim Pattern**.

## The Shim Pattern

A shim is a small layer of C code compiled alongside the Nitpick application that exposes a clean, flat API utilizing only primitive types (primarily `int64_t` and `const char*`).

### Example: Adding a new function

If you want Nitty to call `gtk_window_set_titlebar`:

**1. Add the C implementation (`shim/gtk4/nitty_gtk4_shim.c`)**
```c
void nitty_gtk4_window_set_titlebar(int64_t win_ptr, int64_t titlebar_ptr) {
    GtkWindow *win = (GtkWindow*)win_ptr;
    GtkWidget *titlebar = (GtkWidget*)titlebar_ptr;
    gtk_window_set_titlebar(win, titlebar);
}
```

**2. Add the C declaration (`shim/gtk4/nitty_gtk4_shim.h`)**
```c
void nitty_gtk4_window_set_titlebar(int64_t win_ptr, int64_t titlebar_ptr);
```

**3. Add the Nitpick binding (`src/gui/gtk4_ffi.npk`)**
```nitpick
extern func:nitty_gtk4_window_set_titlebar = NIL(int64:win_ptr, int64:titlebar_ptr);
```

You can now call `nitty_gtk4_window_set_titlebar(win, header)` natively from anywhere in Nitpick.

## Memory Management and Pointers

Nitpick integers (`int64`) are used to hold C pointers.

- **Opaque Pointers:** When C returns a widget (e.g., `GtkWidget*`), the shim casts it to `int64_t`. Nitpick treats this as an opaque handle and passes it back to C functions unmodified.
- **Ownership:** C libraries usually manage the memory for these objects (e.g., GTK's `GObject` reference counting). Never try to free these handles from Nitpick directly unless the shim explicitly provides a `nitty_free_object` function.

## String Handling

Passing strings between Nitpick and C requires care.

- **Nitpick → C:** Nitpick strings are inherently UTF-8 and null-terminated. When passing a `string` to an `extern func` that expects `const char*`, Nitpick handles the translation automatically.
- **C → Nitpick:** When C returns a `const char*`, Nitpick automatically copies it into a new, garbage-collected Nitpick string. **Crucially, Nitpick assumes the C string is null-terminated.** Do not return pointers to un-terminated buffers.
- **Memory Safety:** If a C function requires a string pointer that it will retain indefinitely (e.g., saving a static label), you must copy that string via `strdup` inside the C shim, because the Nitpick string memory might be collected.

## Error Propagation

Nitpick handles errors using the `Result<T>` type, but C uses return codes (often `-1` or `NULL`).

The shim should return the standard C error code, and the corresponding `.npk` wrapper should translate it:

```nitpick
// In Nitpick wrapper file
pub func:open_port = Result<int64>(string:path) {
    int64:fd = raw nitty_serial_open(path);
    if (fd < 0i64) {
        fail(1i32, "Failed to open serial port");
    }
    pass(fd);
};
```

## Existing Shims

- **`shim/gtk4/nitty_gtk4_shim.c`:** The largest shim. Wraps GTK4 widgets, the Cairo drawing context, Pango text rendering, the GLib main loop, and AT-SPI accessibility events.
- **`shim/libssh2/nitty_ssh2_shim.c`:** Wraps LibSSH2 for the Connection layer. Exposes non-blocking socket reads, auth callbacks, SFTP, and port forwarding.
- **`shim/serial/nitty_serial_shim.c`:** Wraps standard POSIX termios and ioctl calls for the Serial client.
- **`shim/pty/nitty_pty_shim.c`:** Wraps `forkpty` and standard POSIX unistd calls for local terminal sessions.
- **`shim/bench/nitty_bench_shim.c`:** Exposes high-resolution monotonic clocks (`clock_gettime`) and `/proc/self/statm` reads for the performance benchmarking suite.
