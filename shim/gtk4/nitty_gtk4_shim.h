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

/* =======================================================================
 * v0.4.1: Multi-tab PTY lifecycle
 * ======================================================================= */

/**
 * Spawn a new PTY and shell for a new tab.
 * Returns (master_fd * 1000000 + pid) so Nitpick can unpack both values.
 * Returns -1 on failure.
 * The new shell is NOT made the active PTY — call nitty_gtk4_set_active_pty_fd().
 */
int64_t nitty_gtk4_spawn_tab_shell(int64_t rows, int64_t cols);

/**
 * Unpack the master_fd from a nitty_gtk4_spawn_tab_shell return value.
 */
int64_t nitty_gtk4_spawn_result_fd(int64_t result);

/**
 * Unpack the child_pid from a nitty_gtk4_spawn_tab_shell return value.
 */
int64_t nitty_gtk4_spawn_result_pid(int64_t result);

/**
 * Switch the active PTY: the idle output poll and input writes will
 * now use this master_fd/pid pair. Clears the PTY byte queue.
 * Pass -1/-1 for master_fd/pid to disable.
 */
void nitty_gtk4_set_active_pty_fd(int64_t master_fd, int64_t child_pid);

/**
 * Kill a tab's shell process and close its PTY master fd.
 * Sends SIGHUP first, then SIGKILL after 100ms if still alive.
 * This does NOT affect the active PTY — caller must switch first.
 */
void nitty_gtk4_kill_tab_shell(int64_t master_fd, int64_t child_pid);

/**
 * Show a synchronous confirmation dialog for tab closure.
 * Returns 1 if the user confirmed "Close", 0 if they cancelled.
 */
int64_t nitty_gtk4_confirm_close(const char *tab_title);

/* =======================================================================
 * v0.4.3: Context menu, rename dialog, duplicate-tab CWD, completion poll
 * ======================================================================= */

/**
 * Show the tab right-click context menu at (x, y) relative to the DrawingArea.
 * tab_idx is the display index of the right-clicked tab.
 * The menu is async (GtkPopoverMenu); use nitty_gtk4_context_menu_poll() to
 * receive the selected action on the next idle tick.
 */
void nitty_gtk4_context_menu_show(int64_t x, int64_t y, int64_t tab_idx);

/**
 * Poll for a pending context menu action.
 * Returns:
 *   0  = no pending action
 *   1  = Rename Tab
 *   2  = Duplicate Tab
 *   3  = Close Tab
 *   4  = Close Other Tabs
 *   5  = Close Tabs to the Right
 *   6  = Set color: Red       (0xE05555)
 *   7  = Set color: Orange    (0xE08C40)
 *   8  = Set color: Yellow    (0xD4C05A)
 *   9  = Set color: Green     (0x5AC85A)
 *  10  = Set color: Blue      (0x4080D0)
 *  11  = Set color: Purple    (0x9060D0)
 *  12  = Set color: Pink      (0xD060A0)
 *  13  = Clear color (None)
 * Calling this function clears the pending action.
 */
int64_t nitty_gtk4_context_menu_poll(void);

/**
 * Return the tab display index that was right-clicked to open the context menu.
 * Valid only after nitty_gtk4_context_menu_poll() returns non-zero.
 */
int64_t nitty_gtk4_context_menu_get_tab_idx(void);

/**
 * Show a synchronous text-input dialog (for tab rename).
 * current_title: pre-filled entry text.
 * Returns the new title string entered by the user, or "" if cancelled.
 * The returned pointer is valid until the next call to this function.
 */
const char *nitty_gtk4_prompt_string(const char *prompt, const char *current_value);

/**
 * Spawn a new PTY+shell with its working directory set to /proc/<source_pid>/cwd.
 * Falls back to the user's home directory if the CWD cannot be read.
 * Returns (master_fd * 1000000 + pid), same as nitty_gtk4_spawn_tab_shell.
 * Returns -1 on failure.
 */
int64_t nitty_gtk4_spawn_tab_shell_at_cwd(int64_t rows, int64_t cols, int64_t source_pid);

/**
 * Poll for shell completion events (tabs whose child process has exited).
 * Returns the display-slot index of a tab whose shell has exited, or -1 if none.
 * Only reports inactive tabs (active tab exits are handled separately).
 * Calling this clears the reported event.
 * NOTE: The caller must register tab PIDs via nitty_gtk4_register_tab_pid().
 */
int64_t nitty_gtk4_completion_event_poll(void);

/**
 * Register a tab's PID so the completion poller can watch it.
 * slot: display index [0..15]. pid: shell child PID.
 */
void nitty_gtk4_register_tab_pid(int64_t slot, int64_t pid);

/**
 * Unregister a tab slot's PID (called on tab close).
 */
void nitty_gtk4_unregister_tab_pid(int64_t slot);

/**
 * v0.4.4: Session persistence helpers.
 */

/**
 * Read the working directory of a process from /proc/<pid>/cwd via readlink.
 * Returns the path as a NUL-terminated string, or "" on error (pid invalid,
 * process gone, or permission denied).  Thread-safe (static buffer protected
 * by the GTK main thread assumption).
 */
const char *nitty_gtk4_get_proc_cwd(int64_t pid);

/**
 * Spawn a new PTY + shell with the working directory set to `path`.
 * Like nitty_gtk4_spawn_tab_shell_at_cwd but takes an explicit path string
 * instead of deriving it from a source PID.  Falls back to $HOME if path
 * is NULL, empty, or does not exist.
 *
 * Returns (master_fd * 1000000 + child_pid), or -1 on failure.
 */
int64_t nitty_gtk4_spawn_tab_shell_at_path(int64_t rows, int64_t cols,
                                           const char *path);

/**
 * Return CLOCK_MONOTONIC seconds as an int64_t.
 * Used for periodic session save throttling in terminal_widget.npk.
 */
int64_t nitty_gtk4_monotonic_sec(void);

/* ═══════════════════════════════════════════════════════════════════════════
 * v0.5.1: Split pane support
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * Pane event queue poll.
 * Returns: 0=none, 16=split_horiz, 17=split_vert, 18=close_pane_or_tab
 * Calling this clears the pending event.
 */
int64_t nitty_gtk4_pane_event_poll(void);

/**
 * Create a GtkPaned widget.
 * orientation: 0=GTK_ORIENTATION_HORIZONTAL, 1=GTK_ORIENTATION_VERTICAL
 * Returns a GtkWidget pointer cast to int64_t, or 0 on failure.
 */
int64_t nitty_gtk4_paned_new(int64_t orientation);

/**
 * Set the start child (left or top) of a GtkPaned.
 */
void nitty_gtk4_paned_set_start_child(int64_t paned_ptr, int64_t child_ptr);

/**
 * Set the end child (right or bottom) of a GtkPaned.
 */
void nitty_gtk4_paned_set_end_child(int64_t paned_ptr, int64_t child_ptr);

/**
 * Set the divider position in pixels.
 */
void nitty_gtk4_paned_set_position(int64_t paned_ptr, int64_t position);

/**
 * Get the current divider position in pixels.
 */
int64_t nitty_gtk4_paned_get_position(int64_t paned_ptr);

/**
 * Create a new GtkDrawingArea for a pane, NOT wired to the primary render
 * callback. Returns ptr cast to int64_t.
 * The caller owns the widget (it is not yet parented).
 */
int64_t nitty_gtk4_pane_drawing_area_new(void);

/**
 * Replace the window's current child widget with a new one.
 * Used when rebuilding the split layout. The old child is automatically
 * unparented by GTK4 when a new child is set.
 */
void nitty_gtk4_set_content_widget(int64_t widget_ptr);

/**
 * Retrieve the current content widget (child of the main window,
 * below the tab bar). Returns 0 if not set.
 */
int64_t nitty_gtk4_get_content_widget(void);

/**
 * Create a GtkBox container.
 * orientation: 0=GTK_ORIENTATION_HORIZONTAL, 1=GTK_ORIENTATION_VERTICAL
 * spacing: pixel spacing between children
 */
int64_t nitty_gtk4_box_new(int64_t orientation, int64_t spacing);

/**
 * Append a child widget to a GtkBox.
 */
void nitty_gtk4_box_append(int64_t box_ptr, int64_t child_ptr);

/**
 * Set widget to expand (fill_h=1 → horizontal, fill_v=1 → vertical).
 */
void nitty_gtk4_widget_set_expand(int64_t widget_ptr, int64_t fill_h, int64_t fill_v);

#ifdef __cplusplus
}
#endif

#endif /* NITTY_GTK4_SHIM_H */

