/*
 * nitty_gtk4_shim.h — GTK4 C FFI shim for Nitty terminal emulator
 *
 * v0.0.2: Added DrawingArea, Cairo drawing, Pango text rendering,
 *         and high-DPI scale factor support.
 *
 * All pointers are opaque int64_t handles.
 * Floating-point values use fixed-point encoding (value * 1000).
 */

#ifndef NITTY_GTK4_SHIM_H
#define NITTY_GTK4_SHIM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══════════════════════════════════════════════════════════════════════
 * v0.0.1: Application lifecycle + Window management
 * ═══════════════════════════════════════════════════════════════════════ */

/* Pre-run configuration */
void nitty_gtk4_configure_window(const char *title, int64_t width, int64_t height);

/* Application lifecycle */
int64_t nitty_gtk4_app_new(const char *app_id);
int64_t nitty_gtk4_app_run(int64_t app_ptr);
void    nitty_gtk4_app_quit(int64_t app_ptr);
void    nitty_gtk4_app_free(int64_t app_ptr);

/* Window access */
int64_t nitty_gtk4_get_main_window(void);

/* Window management */
int64_t nitty_gtk4_window_new(int64_t app_ptr);
void    nitty_gtk4_window_set_title(int64_t win_ptr, const char *title);
void    nitty_gtk4_window_set_size(int64_t win_ptr, int64_t width, int64_t height);
void    nitty_gtk4_window_show(int64_t win_ptr);
void    nitty_gtk4_window_close(int64_t win_ptr);
int64_t nitty_gtk4_window_get_width(int64_t win_ptr);
int64_t nitty_gtk4_window_get_height(int64_t win_ptr);

/* Callback stub (v0.1.x) */
void nitty_gtk4_set_activate_callback(int64_t app_ptr, void (*callback)(int64_t));

/* ═══════════════════════════════════════════════════════════════════════
 * v0.0.3: Input enable (flag — actual controller creation in on_activate)
 * ═══════════════════════════════════════════════════════════════════════ */

/**
 * Enable keyboard and mouse input controllers.
 * Must be called before nitty_gtk4_app_run().
 * Controllers are created inside on_activate when GTK is initialized.
 */
void nitty_gtk4_input_enable(void);

/**
 * Process one pending GTK event (non-blocking).
 * Returns 1 if there are more events pending, 0 if queue is empty.
 * Used by Nitpick to drive event processing between iterations.
 * Note: Not needed when nitty is in normal GTK main loop mode —
 *       this is for the debug/input-test loop in v0.0.3.
 */
int64_t nitty_gtk4_iteration(void);

/* ═══════════════════════════════════════════════════════════════════════
 * v0.0.2: DrawingArea, Cairo, Pango, Scale Factor
 * ═══════════════════════════════════════════════════════════════════════ */

/* ── DrawingArea ──────────────────────────────────────────────────────── */

/** Create a GtkDrawingArea widget. Returns opaque widget pointer. */
int64_t nitty_gtk4_drawing_area_new(void);

/** Set the DrawingArea content size (minimum size request). */
void nitty_gtk4_drawing_area_set_size(int64_t da_ptr, int64_t w, int64_t h);

/**
 * Register the draw callback for the DrawingArea.
 * The callback stores the cairo_t and dimensions in statics.
 * Nitpick code calls nitty_render_frame() to paint during the callback.
 */
void nitty_gtk4_drawing_area_set_draw_func(int64_t da_ptr);

/** Request a widget redraw. */
void nitty_gtk4_widget_queue_draw(int64_t widget_ptr);

/** Set a widget as the window's child (replaces any previous child). */
void nitty_gtk4_window_set_child(int64_t win_ptr, int64_t widget_ptr);

/* ── Draw state (valid only during the draw callback) ─────────────────── */

/** Get the current cairo_t during a draw callback. Returns 0 outside draw. */
int64_t nitty_gtk4_get_draw_cr(void);

/** Get the DrawingArea width during the current draw callback. */
int64_t nitty_gtk4_get_draw_width(void);

/** Get the DrawingArea height during the current draw callback. */
int64_t nitty_gtk4_get_draw_height(void);

/* ── Render callback registration ─────────────────────────────────────── */

/**
 * Register a C function pointer that will be called on each draw frame.
 * Signature: void (*render_fn)(void)
 * The render function should use nitty_cairo_* and nitty_pango_* to paint.
 */
void nitty_gtk4_set_render_callback(void (*render_fn)(void));

/* ── Cairo drawing (fixed-point: multiply float by 1000) ──────────────── */

/** Set RGB source color. r/g/b are fixed-point [0..1000] mapping to [0.0..1.0]. */
void nitty_cairo_set_source_rgb(int64_t cr, int64_t r_fp, int64_t g_fp, int64_t b_fp);

/** Paint the entire surface with the current source color. */
void nitty_cairo_paint(int64_t cr);

/** Add a rectangle to the current path (all coords fixed-point * 1000). */
void nitty_cairo_rectangle(int64_t cr, int64_t x_fp, int64_t y_fp, int64_t w_fp, int64_t h_fp);

/** Fill the current path and clear it. */
void nitty_cairo_fill(int64_t cr);

/** Move the current point (fixed-point * 1000). */
void nitty_cairo_move_to(int64_t cr, int64_t x_fp, int64_t y_fp);

/* ── Pango text rendering ─────────────────────────────────────────────── */

/** Create a PangoLayout from a cairo context. Returns opaque layout pointer. */
int64_t nitty_pango_layout_new(int64_t cr);

/** Set the font on a PangoLayout. font_desc is e.g. "Monospace 12". */
void nitty_pango_layout_set_font(int64_t layout, const char *font_desc);

/** Set the text content of a PangoLayout. */
void nitty_pango_layout_set_text(int64_t layout, const char *text);

/**
 * Get the pixel size of the layout. Returns encoded: width * 65536 + height.
 * Caller extracts: width = result / 65536, height = result % 65536.
 */
int64_t nitty_pango_layout_get_pixel_size(int64_t layout);

/** Render the PangoLayout at the current cairo point. */
void nitty_pango_show_layout(int64_t cr, int64_t layout);

/** Free a PangoLayout. */
void nitty_pango_layout_destroy(int64_t layout);

/* ── Scale factor ─────────────────────────────────────────────────────── */

/** Get the integer scale factor for a widget (for HiDPI). */
int64_t nitty_gtk4_widget_get_scale_factor(int64_t widget_ptr);

/* ═══════════════════════════════════════════════════════════════════════
 * v0.0.4: Terminal grid, resize, key-to-grid
 * ═══════════════════════════════════════════════════════════════════════ */

/**
 * Enable the terminal grid rendering mode.
 * Must be called before nitty_gtk4_app_run().
 * Grid is initialized in on_activate after DrawingArea is created.
 * @param font_desc  Pango font description (e.g. "Monospace 12")
 */
void nitty_gtk4_grid_enable(const char *font_desc);

/** Return 1 if a resize event is pending, 0 otherwise. Clears pending. */
int64_t nitty_gtk4_resize_poll(void);

/** Get the new width after a resize event. */
int64_t nitty_gtk4_resize_get_width(void);

/** Get the new height after a resize event. */
int64_t nitty_gtk4_resize_get_height(void);

/**
 * Process a key press and write it to the grid.
 * Handles printable chars, Enter, Backspace, arrow keys.
 */
void nitty_gtk4_grid_handle_key(int64_t keyval, int64_t modifiers);

/* ═══════════════════════════════════════════════════════════════════════
 * v0.1.4: fd watching, idle callback, monotonic time
 * ═══════════════════════════════════════════════════════════════════════ */

/**
 * Watch a file descriptor for readability using GLib's event loop.
 * When the fd becomes readable, sets an internal flag.
 * Returns 0 on success, -1 on error.
 */
int64_t nitty_gtk4_add_fd_watch(int64_t fd);

/** Return 1 if the watched fd has data ready, 0 otherwise. */
int64_t nitty_gtk4_fd_watch_poll(void);

/** Clear the fd-ready flag. */
void nitty_gtk4_fd_watch_clear(void);

/** Remove the fd watch. */
void nitty_gtk4_fd_watch_remove(void);

/** Get monotonic time in microseconds. */
int64_t nitty_gtk4_get_monotonic_time(void);

/**
 * Register an idle callback that fires every frame.
 * The callback pointer is called each GLib main loop iteration.
 * Used to poll PTY output and process it.
 */
void nitty_gtk4_set_idle_callback(void (*callback)(void));

/** Enable terminal mode — spawn shell in on_activate after grid init. */
void nitty_gtk4_terminal_enable(void);

/** Request a redraw of the DrawingArea. */
void nitty_gtk4_queue_redraw(void);

/* =======================================================================
 * v0.3.0: PTY byte queue (for Nitpick pipeline)
 * ======================================================================= */

/**
 * How many raw PTY bytes are pending in the Nitpick byte queue.
 * These are the same bytes that were also sent to nitty_grid_process_output.
 * Nitpick calls this each render frame to decide if the pipeline needs feeding.
 */
int64_t nitty_gtk4_pty_bytes_available(void);

/**
 * Dequeue one byte from the PTY output queue.
 * Returns [0..255] for a valid byte, or -1 if the queue is empty.
 * Call in a loop until -1 to drain all pending bytes each frame.
 */
int64_t nitty_gtk4_pty_poll_byte(void);

/**
 * v0.3.3: Write one byte to the PTY master fd.
 * Used by Nitpick pipeline for responses (DECSET 1004 focus reporting, etc.).
 * Returns 1 on success, -1 if no PTY is active.
 */
int64_t nitty_gtk4_pty_write_byte(int64_t byte_val);

/* =======================================================================
 * v0.3.3: Focus controller
 * ======================================================================= */

/**
 * Enable focus tracking on the DrawingArea.
 * Call once after nitty_gtk4_window_show() so the drawing area exists.
 * Registers enter/leave callbacks on a GtkEventControllerFocus.
 */
void nitty_gtk4_focus_enable(void);

/**
 * Return 1 if the terminal drawing area currently has keyboard focus,
 * 0 if it is unfocused. Used by renderer.npk to draw hollow cursor.
 */
int64_t nitty_gtk4_get_focused(void);

#ifdef __cplusplus
}
#endif

#endif /* NITTY_GTK4_SHIM_H */
