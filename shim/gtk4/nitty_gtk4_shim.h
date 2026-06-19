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
 * Read the process name from /proc/<pid>/comm.
 * Returns a pointer to a static buffer with the comm string (max 15 chars),
 * stripped of trailing newline.  Returns "" on any error.
 * Safe only on the GTK main thread (uses a static buffer).
 */
const char *nitty_gtk4_get_proc_comm(int64_t pid);

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

/* ═══════════════════════════════════════════════════════════════════════
 * v0.5.3: Notification toast
 * ═══════════════════════════════════════════════════════════════════════ */

/**
 * Show a brief info message in the terminal title bar area.
 * The message auto-dismisses after ~2 seconds.
 */
void nitty_gtk4_show_notification(const char *msg);

/* ═══════════════════════════════════════════════════════════════════════
 * v0.5.4: Broadcast input control
 * ═══════════════════════════════════════════════════════════════════════ */

/**
 * Begin a new broadcast fd list (clears any previous list).
 * Call this before nitty_gtk4_broadcast_add_fd() calls.
 */
void nitty_gtk4_broadcast_begin(void);

/**
 * Add a PTY master fd to the broadcast list.
 * Keys typed in the active pane will also be written to this fd.
 */
void nitty_gtk4_broadcast_add_fd(int64_t fd);

/**
 * Clear all broadcast fds (disable broadcast).
 */
void nitty_gtk4_broadcast_clear(void);

/**
 * Returns 1 if broadcast is currently active (at least one extra fd), else 0.
 */
int64_t nitty_gtk4_broadcast_is_active(void);

/**
 * v0.5.5: Notify the input handler whether swap mode is active.
 * When active=1, the next Escape keypress fires pane_event=30 instead of
 * going to the PTY. Called by Nitpick when entering/leaving swap mode.
 */
void nitty_gtk4_set_swap_mode(int64_t active);

/**
 * v0.6.1: Profile-aware tab spawn.
 * Spawns a PTY+shell with an explicit shell binary AND working directory.
 * shell_bin: path to shell executable ("" or NULL = use $SHELL).
 * cwd:       initial working directory ("" or NULL = use $HOME).
 * Returns (master_fd * 1000000 + pid), or -1 on failure.
 * Falls back through: shell_cmd → spawn_at (cwd only) → spawn_shell.
 */
int64_t nitty_gtk4_spawn_tab_shell_cmd(int64_t rows, int64_t cols,
                                        const char *cwd, const char *shell_bin);

/**
 * v0.6.2: Hotkey engine support.
 *
 * nitty_gtk4_get_monotonic_ms: monotonic clock in milliseconds (for chord timeout).
 * nitty_gtk4_key_was_consumed:  returns 1 if the last key event was intercepted
 *   by the C shim (tab/pane/scroll event fired), 0 if it passed through.
 *   Prevents the Nitpick hotkey engine from double-dispatching intercepted keys.
 * nitty_gtk4_clipboard_copy:   legacy no-op stub (superseded by copy_text).
 * nitty_gtk4_clipboard_paste:  legacy no-op stub (superseded by paste_request).
 */
int64_t nitty_gtk4_get_monotonic_ms(void);
int64_t nitty_gtk4_key_was_consumed(void);
void    nitty_gtk4_clipboard_copy(void);
void    nitty_gtk4_clipboard_paste(void);

/**
 * v0.7.1: Full clipboard copy/paste implementation.
 *
 * nitty_gtk4_clipboard_copy_text(text):    write text to system clipboard.
 *   Also writes to X11 PRIMARY selection automatically.
 * nitty_gtk4_clipboard_paste_request():    begin async clipboard read.
 * nitty_gtk4_primary_paste_request():      begin async PRIMARY selection read.
 * nitty_gtk4_clipboard_paste_ready():      returns 1 when paste text is available.
 * nitty_gtk4_clipboard_paste_text_len():   byte length of pending paste text.
 * nitty_gtk4_clipboard_paste_get_byte(i):  read byte i of paste text.
 *
 * v0.7.1: Mouse helpers.
 * nitty_gtk4_mouse_get_n_press():  click count (1=single, 2=double, 3=triple).
 * nitty_gtk4_key_modifiers_alt():  1 if Alt was held during last key/mouse event.
 */
void    nitty_gtk4_clipboard_copy_text(const char *text);
void    nitty_gtk4_clipboard_paste_request(void);
void    nitty_gtk4_primary_paste_request(void);
int64_t nitty_gtk4_clipboard_paste_ready(void);
int64_t nitty_gtk4_clipboard_paste_text_len(void);
int64_t nitty_gtk4_clipboard_paste_get_byte(int64_t offset);
int64_t nitty_gtk4_mouse_get_n_press(void);
int64_t nitty_gtk4_key_modifiers_alt(void);


/**
 * v0.6.3: Global hotkey registration (X11 XGrabKey) and Quake mode.
 *
 * nitty_x11_grab_key:          Register a global hotkey via XGrabKey on the root window.
 *                              keyval: GDK keyval (e.g. GDK_KEY_F12).
 *                              mod_mask: GDK modifier mask (e.g. 0 = no mods).
 *                              Returns 1 on success, 0 on failure.
 * nitty_x11_ungrab_key:        Unregister the global hotkey.
 * nitty_global_hotkey_poll:    Returns 1 if the global hotkey fired since last poll, 0 otherwise.
 *                              Clears the fired flag on read.
 * nitty_global_hotkey_is_x11:  Returns 1 if the GDK backend is X11, 0 if Wayland/other.
 *                              Used to decide which registration path to take.
 *
 * nitty_quake_setup_window:    Set window hints for quake mode (no decorations, skip taskbar).
 *                              Must be called after the window is realized.
 * nitty_quake_move_window:     Move the quake window to (x, y) in screen coordinates.
 *                              Only works on X11; no-op on Wayland.
 * nitty_quake_get_monitor_w:   Primary monitor width in pixels.
 * nitty_quake_get_monitor_h:   Primary monitor height in pixels.
 */
int64_t nitty_x11_grab_key(int64_t keyval, int64_t mod_mask);
void    nitty_x11_ungrab_key(int64_t keyval, int64_t mod_mask);
int64_t nitty_global_hotkey_poll(void);
int64_t nitty_global_hotkey_is_x11(void);

void    nitty_quake_setup_window(int64_t win_ptr);
void    nitty_quake_move_window(int64_t win_ptr, int64_t x, int64_t y);
int64_t nitty_quake_get_monitor_w(void);
int64_t nitty_quake_get_monitor_h(void);

/* ═══════════════════════════════════════════════════════════════════════════
 * v0.6.5: Settings window
 *
 * The settings window is a non-modal GTK4 window with a GtkNotebook.
 * Because Nitpick cannot register GTK signal callbacks directly, the
 * window uses a polling model:
 *
 *   1. Nitpick calls nitty_settings_init_values() with current config.
 *   2. Nitpick calls nitty_settings_add_theme() for each built-in theme.
 *   3. Nitpick calls nitty_settings_open() to create + show the window.
 *   4. Each frame, Nitpick calls nitty_settings_poll_event() to check
 *      for button clicks (Apply=1, OK=2, Cancel=3).
 *   5. On Apply or OK, Nitpick reads new values via nitty_settings_get_*().
 *
 * nitty_settings_open:        Create and show the settings window.
 *                             parent_win: main window ptr (for transient-for).
 *                             Returns 1 on success, 0 on failure.
 * nitty_settings_close:       Close and destroy the settings window.
 * nitty_settings_is_open:     Returns 1 if the window is currently open.
 * nitty_settings_poll_event:  Returns 1=Apply, 2=OK, 3=Cancel, 0=none.
 *                             Clears the event on read.
 *
 * nitty_settings_init_values: Populate widget values before opening.
 *                             Call this BEFORE nitty_settings_open().
 * nitty_settings_clear_themes: Clear the theme dropdown items.
 * nitty_settings_add_theme:   Append a theme name to the dropdown.
 *
 * nitty_settings_get_font_family:    Read current font family entry.
 * nitty_settings_get_font_size:      Read current font size spinbutton (int).
 * nitty_settings_get_scrollback:     Read scrollback lines spinbutton.
 * nitty_settings_get_shell:          Read shell entry text.
 * nitty_settings_get_cursor_style:   "block", "underline", or "bar".
 * nitty_settings_get_cursor_blink:   0 or 1.
 * nitty_settings_get_columns:        Terminal columns (int).
 * nitty_settings_get_rows:           Terminal rows (int).
 * nitty_settings_get_theme:          Selected theme name string.
 * nitty_settings_get_opacity:        Opacity 0-1000 fixed-point.
 * nitty_settings_get_close_on_exit:  0 or 1.
 * nitty_settings_get_confirm_close:  0 or 1.
 * ═══════════════════════════════════════════════════════════════════════════ */

int64_t     nitty_settings_open(int64_t parent_win_ptr);
void        nitty_settings_close(void);
int64_t     nitty_settings_is_open(void);
int64_t     nitty_settings_poll_event(void);

void        nitty_settings_init_values(
                const char *font_family, int64_t font_size,
                int64_t scrollback, const char *shell,
                const char *cursor_style, int64_t cursor_blink,
                int64_t columns, int64_t rows,
                const char *theme, int64_t opacity,
                int64_t close_on_exit, int64_t confirm_close);

void        nitty_settings_clear_themes(void);
void        nitty_settings_add_theme(const char *name);

const char *nitty_settings_get_font_family(void);
int64_t     nitty_settings_get_font_size(void);
int64_t     nitty_settings_get_scrollback(void);
const char *nitty_settings_get_shell(void);
const char *nitty_settings_get_cursor_style(void);
int64_t     nitty_settings_get_cursor_blink(void);
int64_t     nitty_settings_get_columns(void);
int64_t     nitty_settings_get_rows(void);
const char *nitty_settings_get_theme(void);
int64_t     nitty_settings_get_opacity(void);
int64_t     nitty_settings_get_close_on_exit(void);
int64_t     nitty_settings_get_confirm_close(void);

/* ── Config file watcher (v0.6.5) ──────────────────────────────────────── */

/**
 * Start watching a config file for modifications (GFileMonitor-based).
 * path: absolute path to the config file.
 * Returns 0 on success, -1 on error.
 */
int64_t nitty_gtk4_config_watch_start(const char *path);

/**
 * Poll for a config file change event.
 * Returns 1 if the file was modified (with 200ms debounce), 0 otherwise.
 * Clears the event on read.
 */
int64_t nitty_gtk4_config_watch_poll(void);

/**
 * Stop watching the config file and release resources.
 */
void nitty_gtk4_config_watch_stop(void);

/* ── Search bar — drawn overlay (v0.7.0) ──────────────────────────────── */

/**
 * Draw the search bar overlay at the top-right of the terminal area.
 * Called from tab_bar.npk / search_bar.npk each frame.
 *
 * query:      Current query string (displayed in the text field area).
 * match_info: "X / Y" or "No matches" label text.
 * is_visible: 0=hidden (do nothing), 1=draw.
 * case_on:    1=case-sensitive indicator lit.
 */
void nitty_search_bar_draw(const char *query, const char *match_info,
                            int is_visible, int case_on);

/**
 * Returns 1 if the search bar is currently active (visible), 0 otherwise.
 * Called by nitty_input.c to decide whether to intercept keys.
 */
int nitty_search_bar_is_active(void);

/**
 * Activate / deactivate the search bar.
 * Called from Nitpick via FFI.
 */
void nitty_search_bar_set_active(int active);

/**
 * Poll for the next pending search input event.
 * Returns event type or 0 if none pending:
 *   1 = printable character typed (call nitty_search_event_get_char())
 *   2 = backspace
 *   3 = escape (close search)
 *   4 = enter (next match)
 *   5 = shift+enter (prev match)
 *   6 = case-sensitivity toggle (Ctrl+Shift+C in search mode)
 */
int64_t nitty_search_event_poll(void);

/**
 * Get the Unicode codepoint of the last typed character (event type 1).
 * Only valid immediately after nitty_search_event_poll() returns 1.
 */
int64_t nitty_search_event_get_char(void);

/**
 * Intercept a key event for search mode.
 * Called from nitty_input.c when search bar is active.
 * Returns 1 if the key was consumed (do NOT forward to PTY), 0 if pass-through.
 * Side effect: pushes an event into g_search_event_queue.
 */
int nitty_search_intercept_key(unsigned int keyval, unsigned int state);

/* ── v0.7.2: Clickable links ─────────────────────────────────────────────── */

/**
 * Open a URI with the system default application (non-blocking).
 * Uses g_app_info_launch_default_for_uri (GLib-native, GTK4-safe).
 * Returns 1 on success, 0 on failure.
 */
int64_t nitty_gtk4_open_url(const char *url);

/**
 * Set the cursor on the terminal drawing area.
 * is_pointer=1: hand/pointer cursor (hovering a link).
 * is_pointer=0: default cursor.
 */
void nitty_gtk4_set_cursor_pointer(int64_t is_pointer);



/* ── v0.7.3: Window Features ─────────────────────────────────────────────── */

/**
 * Pre-run: store opacity (fp×1000, 0–1000) to apply in on_activate.
 * Uses gtk_widget_set_opacity(); 1000 = fully opaque, 0 = fully transparent.
 */
void nitty_gtk4_configure_opacity(int64_t opacity_fp1000);

/**
 * Pre-run: store default window size to apply in on_activate via
 * gtk_window_set_default_size(). Pass 0/0 to leave unchanged.
 */
void nitty_gtk4_configure_default_size(int64_t width, int64_t height);

/** Enter fullscreen. */
void    nitty_gtk4_window_fullscreen(int64_t win_ptr);
/** Leave fullscreen. */
void    nitty_gtk4_window_unfullscreen(int64_t win_ptr);
/** Returns 1 if window is currently fullscreen, 0 otherwise. */
int64_t nitty_gtk4_window_is_fullscreen(int64_t win_ptr);

/** Maximize the window. */
void    nitty_gtk4_window_maximize(int64_t win_ptr);
/** Restore (un-maximize) the window. */
void    nitty_gtk4_window_unmaximize(int64_t win_ptr);
/** Returns 1 if window is currently maximized, 0 otherwise. */
int64_t nitty_gtk4_window_is_maximized(int64_t win_ptr);

/**
 * Show or hide window decorations (title bar, borders).
 * decorated=1 → decorations on (default); decorated=0 → borderless.
 */
void nitty_gtk4_window_set_decorated(int64_t win_ptr, int64_t decorated);

/**
 * Set the window icon by XDG icon theme name (e.g. "utilities-terminal").
 */
void nitty_gtk4_window_set_icon_name(int64_t win_ptr, const char *name);

/**
 * Set always-on-top (keep-above) state via _NET_WM_STATE_ABOVE (X11 only).
 * keep_above=1 → raise above other windows; keep_above=0 → restore normal.
 * No-op on Wayland.
 */
void nitty_gtk4_window_set_keep_above(int64_t win_ptr, int64_t keep_above);

/* ── v0.7.4: Bell and process completion notifications ──────────────────── */

/**
 * Ring the system bell (audible beep via GDK).
 * Uses gdk_display_beep() on the default display.
 * No-op if no display is available.
 */
void nitty_gtk4_display_beep(void);

/**
 * Configure the minimum process runtime (ms) before a completion notification
 * fires.  Default: 10000 (10 s).  Set to 0 to notify for all processes.
 */
void nitty_gtk4_proc_set_min_ms(int64_t ms);

/**
 * Poll for a pending process-completion notification message.
 * Returns 1 if a message is waiting; 0 otherwise.
 * Clears the pending message on return.
 */
int32_t nitty_gtk4_proc_notify_poll(void);

/**
 * Copy the most recent process-completion message into buf (max bufsz bytes).
 * Only meaningful after nitty_gtk4_proc_notify_poll() returns 1.
 */
void nitty_gtk4_proc_notify_get(char *buf, int32_t bufsz);

/**
 * Return a pointer to the static process-completion message buffer.
 * Only meaningful after nitty_gtk4_proc_notify_poll() returns 1.
 * Clears the pending message on return.
 * Usable as a string-returning FFI function.
 */
const char *nitty_gtk4_proc_notify_msg(void);

/**
 * Called by the app layer when a new tab shell is spawned, so the process
 * monitor can track its start time for completion duration calculation.
 * slot  : tab slot index (0-15)
 * pid   : shell process PID
 */
void nitty_gtk4_proc_register(int32_t slot, int64_t pid);

/**
 * v0.8.2: SSH Authentication Dialogs
 *
 * nitty_gtk4_prompt_password: Show a masked password/passphrase dialog.
 *   title  - dialog title
 *   prompt - label text shown to user
 *   Returns entered password (static buffer) or "" if cancelled.
 *
 * nitty_gtk4_host_key_dialog: Show a host-key mismatch warning.
 *   host     - hostname/IP of the server
 *   key_type - "ssh-rsa", "ssh-ed25519", etc.
 *   Returns 1 if user chose "Connect Anyway", 0 if cancelled.
 */
const char *nitty_gtk4_prompt_password(const char *title, const char *prompt);
int64_t     nitty_gtk4_host_key_dialog(const char *host, const char *key_type);


/* ── v0.8.3 Connection Manager UI — new additions ───────────────────────── */
/* (paned/box/scrolled_window already exist from v0.5.x — not duplicated)   */

/* GtkListBox */
int64_t     nitty_gtk4_list_box_new(void);
int64_t     nitty_gtk4_list_box_append(int64_t lb, const char *label);
int64_t     nitty_gtk4_list_box_get_selected(int64_t lb);
void        nitty_gtk4_list_box_clear(int64_t lb);
void        nitty_gtk4_list_box_set_group_header(int64_t lb, int64_t row_idx, const char *header);

/* GtkEntry */
int64_t     nitty_gtk4_entry_new(void);
const char *nitty_gtk4_entry_get_text(int64_t entry);
void        nitty_gtk4_entry_set_placeholder(int64_t entry, const char *text);

/* GtkButton */
int64_t nitty_gtk4_button_new(const char *label);
void    nitty_gtk4_button_set_sensitive(int64_t btn, int32_t sensitive);

/* GtkLabel */
int64_t nitty_gtk4_label_new(const char *text);
void    nitty_gtk4_label_set_text(int64_t label, const char *text);

/* Connection Manager composite sidebar */
int64_t nitty_gtk4_cm_create(void);
void    nitty_gtk4_cm_set_visible(int64_t cm, int32_t visible);
int64_t nitty_gtk4_cm_list_box(int64_t cm);
int64_t nitty_gtk4_cm_entry(int64_t cm);
int64_t nitty_gtk4_cm_event_poll(void);
int64_t nitty_gtk4_cm_event_profile_id(void);

/* Sidebar attach — wraps window DrawingArea in GtkPaned with sidebar */
void    nitty_gtk4_sidebar_attach(int64_t win, int64_t drawing_area, int64_t sidebar);
void    nitty_gtk4_sidebar_set_visible(int32_t visible);

/* Profile editor dialog (v0.8.3 — SSH-only, kept for backward compat) */
int32_t     nitty_gtk4_profile_editor_open(const char *name, const char *group,
                const char *host, int64_t port, const char *user,
                const char *auth_method, const char *key_path);
const char *nitty_gtk4_profile_editor_get_name(void);
const char *nitty_gtk4_profile_editor_get_group(void);
const char *nitty_gtk4_profile_editor_get_host(void);
int64_t     nitty_gtk4_profile_editor_get_port(void);
const char *nitty_gtk4_profile_editor_get_user(void);
const char *nitty_gtk4_profile_editor_get_auth(void);
const char *nitty_gtk4_profile_editor_get_key_path(void);

/* Profile editor v2 — multi-type (v0.9.3)
 *
 * conn_type: 0=local, 1=ssh, 2=serial, 3=telnet
 *
 * Shows the appropriate field section based on the type dropdown selection.
 * The dialog is modal; returns 1 on OK, 0 on Cancel.
 * After OK, read back values via the get_* accessors below.
 * The v0.8.3 getters (get_name, get_group, get_host, get_port, get_user,
 * get_auth, get_key_path) still work for SSH fields; the new getters below
 * add type + serial/telnet fields.
 */
int32_t     nitty_gtk4_profile_editor_open_v2(
                const char *name,     const char *group,
                int64_t     conn_type,
                const char *ssh_host, int64_t ssh_port,
                const char *ssh_user, const char *ssh_auth, const char *ssh_key,
                const char *serial_dev,  int64_t serial_baud,
                const char *telnet_host, int64_t telnet_port);

int64_t     nitty_gtk4_profile_editor_get_type(void);
const char *nitty_gtk4_profile_editor_get_serial_dev(void);
int64_t     nitty_gtk4_profile_editor_get_serial_baud(void);
const char *nitty_gtk4_profile_editor_get_telnet_host(void);
int64_t     nitty_gtk4_profile_editor_get_telnet_port(void);


/* ── SFTP Browser panel (v0.8.5) ─────────────────────────────────────────── */

/* Create SFTP browser composite widget (GtkBox with path label, list box,
 * status label, xfer label). Returns widget pointer. */
int64_t nitty_gtk4_sftp_create(void);

/* Attach SFTP panel to the right side of the window (second GtkPaned). */
void    nitty_gtk4_sftp_panel_attach(int64_t win, int64_t drawing_area,
                                      int64_t sftp_panel);
void    nitty_gtk4_sftp_panel_set_visible(int32_t visible);

/* Accessors for internal sub-widgets */
int64_t nitty_gtk4_sftp_path_label(int64_t sftp_widget);
int64_t nitty_gtk4_sftp_list_box(int64_t sftp_widget);
int64_t nitty_gtk4_sftp_status_label(int64_t sftp_widget);
int64_t nitty_gtk4_sftp_xfer_label(int64_t sftp_widget);

/* Event polling — returns event code each frame:
 *   0 = none, 1 = activate entry, 2 = refresh, 3 = download,
 *   4 = upload here, 5 = rename, 6 = delete, 7 = new folder, 8 = up
 */
int64_t nitty_gtk4_sftp_event_poll(int64_t sftp_widget);
int64_t nitty_gtk4_sftp_event_row(void);   /* row index of last event */

/* ── v0.8.6: X11 Forwarding socket relay ────────────────────────────────── */

/**
 * Connect to the local X11 Unix socket for the given DISPLAY string.
 * display_str: e.g. ":0", ":1.0", "localhost:0"
 * Returns an open socket fd on success, -1 on failure.
 */
int64_t nitty_x11_connect_display(const char *display_str);

/**
 * Read up to len bytes from the X11 socket fd into a static internal buffer.
 * Returns a pointer to the data as a C string (may contain NUL bytes),
 * or "" if nothing available / error. Sets *out_len to actual bytes read.
 * For Nitpick FFI: returns string; caller should use string_length() for length.
 */
const char *nitty_x11_read(int32_t fd, int32_t len);

/**
 * Write data (len bytes) to the X11 socket fd.
 * Returns bytes written, or -1 on error.
 */
int64_t nitty_x11_write(int32_t fd, const char *data, int32_t len);

/**
 * Close the X11 socket fd. Returns 0 on success.
 */
int64_t nitty_x11_close(int32_t fd);

/**
 * Return the value of the DISPLAY environment variable, or "" if not set.
 */
const char *nitty_x11_get_display(void);

/**
 * Read the MIT-MAGIC-COOKIE-1 for the given display from ~/.Xauthority.
 * Returns the 32-char hex cookie, or "" on failure.
 */
const char *nitty_x11_read_auth_cookie(const char *display);

/* ── v0.8.6: Encrypted vault (AES-256-GCM + PBKDF2-SHA256) ─────────────── */

/**
 * Encrypt plaintext using AES-256-GCM with a PBKDF2-SHA256-derived key.
 * Key derivation: PBKDF2(passphrase, random_salt, 100000, 32).
 * Returns base64(salt[16] || iv[12] || tag[16] || ciphertext) in a static buffer,
 * or "" on failure. Thread-unsafe (static buffer).
 */
const char *nitpick_vault_encrypt(const char *passphrase, const char *plaintext);

/**
 * Decrypt a vault blob produced by nitpick_vault_encrypt.
 * blob_b64: base64(salt || iv || tag || ciphertext).
 * Returns the plaintext in a static buffer, or "" on failure.
 */
const char *nitpick_vault_decrypt(const char *passphrase, const char *blob_b64);

/**
 * Return current epoch seconds (time(NULL)) as int64_t.
 * Used by ssh_vault.npk for auto-lock timeout.
 */
int64_t nitty_epoch_seconds(void);

/* ── v0.8.6: Zlib compress / decompress ─────────────────────────────────── */

/**
 * Compress data (len bytes) using zlib deflate (level 6).
 * Returns the compressed bytes as a string in a static buffer.
 * Sets g_zlib_last_out_len to the compressed length.
 * Returns "" on failure.
 */
const char *nitpick_zlib_compress(const char *data, int32_t len);

/**
 * Decompress zlib deflate data (len bytes), up to max_out bytes.
 * Returns the decompressed bytes as a string in a static buffer.
 * Sets g_zlib_last_out_len to the decompressed length.
 * Returns "" on failure.
 */
const char *nitpick_zlib_decompress(const char *data, int32_t len, int32_t max_out);

/**
 * Return the actual byte length of the last compress/decompress output.
 * Since the Nitpick string type may contain NUL bytes, use this instead
 * of string_length() on binary compressed data.
 */
int64_t nitpick_zlib_last_len(void);


/* ── v0.9.4: Serial toolbar (GtkActionBar) ─────────────────────────────── */

/**
 * Create the serial toolbar GtkActionBar and integrate it into the window layout.
 * Must be called once after the main DrawingArea has been set as the window child
 * (i.e. after nitty_gtk4_sidebar_attach / nitty_gtk4_sftp_panel_attach). Wraps
 * the current content widget in a vertical GtkBox with the ActionBar at the bottom.
 * The toolbar is initially hidden.
 */
void    nitty_serial_toolbar_create(void);

/** Show the serial toolbar. Call when a serial pane becomes active. */
void    nitty_serial_toolbar_show(void);

/** Hide the serial toolbar. Call when a non-serial pane becomes active. */
void    nitty_serial_toolbar_hide(void);

/**
 * Sync toolbar widget states to the current session.
 * baud: current baud (matched against standard values; unrecognized shown as-is)
 * dtr_state: 1=asserted, 0=cleared  (reflected by DTR toggle button appearance)
 * rts_state: 1=asserted, 0=cleared
 * input_mode: 0=Raw, 1=Text, 2=Readline  (dropdown selection)
 * output_mode: 0=Text, 1=Hexdump         (dropdown selection)
 */
void    nitty_serial_toolbar_update(int64_t baud, int64_t dtr_state,
                                    int64_t rts_state, int64_t input_mode,
                                    int64_t output_mode);

/** Poll and clear DTR toggle flag. Returns 1 if DTR button was clicked. */
int64_t nitty_serial_toolbar_poll_dtr(void);

/** Poll and clear RTS toggle flag. Returns 1 if RTS button was clicked. */
int64_t nitty_serial_toolbar_poll_rts(void);

/** Poll and clear Break flag. Returns 1 if Break button was clicked. */
int64_t nitty_serial_toolbar_poll_break(void);

/**
 * Poll baud rate change. Returns new baud if changed, -1 if unchanged.
 * Clears the flag on read.
 */
int64_t nitty_serial_toolbar_poll_baud(void);

/**
 * Poll input mode change. Returns new mode (0/1/2) if changed, -1 if unchanged.
 */
int64_t nitty_serial_toolbar_poll_input_mode(void);

/**
 * Poll output mode change. Returns new mode (0/1) if changed, -1 if unchanged.
 */
int64_t nitty_serial_toolbar_poll_output_mode(void);

/* ── v0.15.0: Plugin Manager completion ──────────────────────────────────── */

/**
 * Set the text of a GtkEntry widget (pre-fill a value in a settings form).
 * entry: opaque GtkWidget* handle. text: new content (UTF-8).
 */
void nitty_gtk4_entry_set_text(int64_t entry, const char *text);

/**
 * Open a blocking GTK4 file chooser dialog. Returns the chosen path, or ""
 * if the user cancelled. The returned pointer is valid until the next call.
 * title:       dialog window title.
 * filter_name: human-readable filter label (e.g. "Plugin Archives").
 * pattern:     semicolon-separated glob patterns (e.g. "*.zip;*.tar.gz").
 */
const char *nitty_gtk4_file_chooser_open(const char *title,
                                          const char *filter_name,
                                          const char *pattern);

/**
 * Open a modal plugin-settings dialog showing one GtkEntry per setting.
 * Blocks until the user clicks Apply (returns 1) or Cancel (returns 0).
 * plugin_name: label shown in the dialog title bar.
 * keys_blob:   newline-separated setting key names (up to 8).
 * values_blob: newline-separated current values (parallel to keys_blob).
 * count:       number of settings (len of each blob line-list).
 */
int64_t nitty_gtk4_plugin_settings_dialog(const char *plugin_name,
                                           const char *keys_blob,
                                           const char *values_blob,
                                           int64_t     count);

/**
 * Retrieve the value the user entered for setting index i after a successful
 * nitty_gtk4_plugin_settings_dialog() call. Returns "" for out-of-range i.
 */
const char *nitty_gtk4_plugin_settings_get_value(int64_t i);

/**
 * Execute a shell command via system(3). Returns the exit status, or -1 on
 * error. cmd must be a valid POSIX shell command string.
 */
int64_t nitty_sys_exec(const char *cmd);

#ifdef __cplusplus
}
#endif

#endif /* NITTY_GTK4_SHIM_H */
