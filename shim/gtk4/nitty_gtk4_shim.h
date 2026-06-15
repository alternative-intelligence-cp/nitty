/*
 * nitty_gtk4_shim.h — GTK4 C FFI shim for Nitty terminal emulator
 *
 * Thin C wrapper over GTK4 for Nitpick code. All parameters and return
 * values use int64_t (matching Nitpick's int64 type). GTK object pointers
 * are opaque int64_t handles.
 *
 * v0.0.1 design: window configuration is set before app_run(). The internal
 * on_activate() handler creates and shows the window automatically.
 * Full Nitpick callback dispatch is planned for v0.1.x.
 */

#ifndef NITTY_GTK4_SHIM_H
#define NITTY_GTK4_SHIM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Pre-run configuration ────────────────────────────────────────────── */

/**
 * nitty_gtk4_configure_window — Set window title and size before app_run().
 *
 * Call this before nitty_gtk4_app_run(). The internal activate handler
 * will use these values when creating the main window.
 *
 * @param title   Window title string (NULL = keep default "Nitty Terminal")
 * @param width   Window width in pixels (0 = keep default 1024)
 * @param height  Window height in pixels (0 = keep default 768)
 */
void nitty_gtk4_configure_window(const char *title, int64_t width, int64_t height);

/* ── Application lifecycle ────────────────────────────────────────────── */

/**
 * nitty_gtk4_app_new — Create a new GtkApplication.
 *
 * @param app_id  Application ID (e.g. "com.altintel.nitty")
 * @return        Opaque app pointer, or 0 on failure
 */
int64_t nitty_gtk4_app_new(const char *app_id);

/**
 * nitty_gtk4_app_run — Run the GTK main loop (blocks until exit).
 *
 * @param app_ptr  Opaque app pointer from nitty_gtk4_app_new()
 * @return         Application exit status (0 = success)
 */
int64_t nitty_gtk4_app_run(int64_t app_ptr);

/**
 * nitty_gtk4_app_quit — Request the application to quit.
 *
 * @param app_ptr  Opaque app pointer from nitty_gtk4_app_new()
 */
void nitty_gtk4_app_quit(int64_t app_ptr);

/**
 * nitty_gtk4_app_free — Release the GtkApplication reference.
 *
 * Must be called after nitty_gtk4_app_run() returns.
 *
 * @param app_ptr  Opaque app pointer from nitty_gtk4_app_new()
 */
void nitty_gtk4_app_free(int64_t app_ptr);

/* ── Window access ────────────────────────────────────────────────────── */

/**
 * nitty_gtk4_get_main_window — Get the main window created by on_activate().
 *
 * Valid only after nitty_gtk4_app_run() has been called (from signal handlers
 * or after the app exits). Returns 0 if not yet created.
 *
 * @return  Opaque window pointer, or 0 if not available
 */
int64_t nitty_gtk4_get_main_window(void);

/* ── Window management ────────────────────────────────────────────────── */

/**
 * nitty_gtk4_window_new — Create a new GtkApplicationWindow (advanced use).
 *
 * For most cases, use nitty_gtk4_configure_window() + nitty_gtk4_app_run()
 * instead. This is for advanced multi-window scenarios.
 *
 * @param app_ptr  Opaque app pointer from nitty_gtk4_app_new()
 * @return         Opaque window pointer, or 0 on failure
 */
int64_t nitty_gtk4_window_new(int64_t app_ptr);

/** Set the window title bar text. */
void nitty_gtk4_window_set_title(int64_t win_ptr, const char *title);

/** Set the window default size in pixels. */
void nitty_gtk4_window_set_size(int64_t win_ptr, int64_t width, int64_t height);

/** Present the window on screen. */
void nitty_gtk4_window_show(int64_t win_ptr);

/** Close the window. */
void nitty_gtk4_window_close(int64_t win_ptr);

/** Get current window width (-1 on failure). */
int64_t nitty_gtk4_window_get_width(int64_t win_ptr);

/** Get current window height (-1 on failure). */
int64_t nitty_gtk4_window_get_height(int64_t win_ptr);

/* ── Callback registration (v0.1.x) ──────────────────────────────────── */

/**
 * nitty_gtk4_set_activate_callback — Register Nitpick activate callback.
 * Stub in v0.0.1. Full implementation in v0.1.x.
 */
void nitty_gtk4_set_activate_callback(int64_t app_ptr, void (*callback)(int64_t));

#ifdef __cplusplus
}
#endif

#endif /* NITTY_GTK4_SHIM_H */
