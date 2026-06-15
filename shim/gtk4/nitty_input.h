/*
 * nitty_input.h — Keyboard and mouse input for Nitty
 *
 * v0.0.3: GTK4 event controller wrappers for keyboard and mouse.
 *
 * Design: event callbacks store the last event in static globals.
 * Nitpick polls via nitty_gtk4_key_poll() / nitty_gtk4_mouse_poll()
 * and reads individual fields. One event at a time is exposed —
 * sufficient for v0.0.3's debug-output goal.
 *
 * All functions are called from within the GTK main loop context.
 */

#ifndef NITTY_INPUT_H
#define NITTY_INPUT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Controller setup ─────────────────────────────────────────────────── */

/**
 * Attach a GtkEventControllerKey to the given widget.
 * Connects key-pressed and key-released signals.
 * @return  Non-zero on success
 */
int64_t nitty_gtk4_key_controller_new(int64_t widget_ptr);

/**
 * Attach GtkGestureClick, GtkEventControllerScroll, and
 * GtkEventControllerMotion to the given widget.
 * @return  Non-zero on success
 */
int64_t nitty_gtk4_mouse_controllers_new(int64_t widget_ptr);

/* ── Keyboard event polling ───────────────────────────────────────────── */

/** Return 1 if a key event is pending, 0 otherwise. Clears the pending flag. */
int64_t nitty_gtk4_key_poll(void);

/** Return the GDK keyval of the last key event. */
int64_t nitty_gtk4_key_get_keyval(void);

/** Return the hardware keycode of the last key event. */
int64_t nitty_gtk4_key_get_keycode(void);

/** Return the modifier bitmask of the last key event. */
int64_t nitty_gtk4_key_get_modifiers(void);

/** Return 1 for key-press, 2 for key-release. */
int64_t nitty_gtk4_key_get_type(void);

/* ── GDK keyval constants ─────────────────────────────────────────────── */

int64_t nitty_gdk_key_return(void);
int64_t nitty_gdk_key_escape(void);
int64_t nitty_gdk_key_backspace(void);
int64_t nitty_gdk_key_tab(void);
int64_t nitty_gdk_key_space(void);
int64_t nitty_gdk_key_up(void);
int64_t nitty_gdk_key_down(void);
int64_t nitty_gdk_key_left(void);
int64_t nitty_gdk_key_right(void);
int64_t nitty_gdk_key_home(void);
int64_t nitty_gdk_key_end(void);
int64_t nitty_gdk_key_page_up(void);
int64_t nitty_gdk_key_page_down(void);
int64_t nitty_gdk_key_insert(void);
int64_t nitty_gdk_key_delete(void);
int64_t nitty_gdk_key_f1(void);
int64_t nitty_gdk_key_f2(void);
int64_t nitty_gdk_key_f3(void);
int64_t nitty_gdk_key_f4(void);
int64_t nitty_gdk_key_f5(void);
int64_t nitty_gdk_key_f6(void);
int64_t nitty_gdk_key_f7(void);
int64_t nitty_gdk_key_f8(void);
int64_t nitty_gdk_key_f9(void);
int64_t nitty_gdk_key_f10(void);
int64_t nitty_gdk_key_f11(void);
int64_t nitty_gdk_key_f12(void);

/* ── GDK modifier constants ───────────────────────────────────────────── */

int64_t nitty_gdk_mod_shift(void);
int64_t nitty_gdk_mod_ctrl(void);
int64_t nitty_gdk_mod_alt(void);
int64_t nitty_gdk_mod_super(void);

/* ── Mouse event polling ──────────────────────────────────────────────── */

/** Return 1 if a mouse event is pending, 0 otherwise. Clears pending flag. */
int64_t nitty_gtk4_mouse_poll(void);

/** Return event type: 1=click, 2=release, 3=scroll, 4=motion */
int64_t nitty_gtk4_mouse_get_type(void);

/** Return X position (fixed-point × 1000). */
int64_t nitty_gtk4_mouse_get_x(void);

/** Return Y position (fixed-point × 1000). */
int64_t nitty_gtk4_mouse_get_y(void);

/** Return button number: 1=left, 2=middle, 3=right */
int64_t nitty_gtk4_mouse_get_button(void);

/** Return horizontal scroll delta (fixed-point × 1000). */
int64_t nitty_gtk4_mouse_get_scroll_dx(void);

/** Return vertical scroll delta (fixed-point × 1000). */
int64_t nitty_gtk4_mouse_get_scroll_dy(void);

/* ── Terminal mode (v0.1.4) ───────────────────────────────────────────── */

/** Enable terminal mode: key events route to PTY fd instead of grid. */
void nitty_input_set_terminal_mode(int64_t master_fd);

/** Disable terminal mode: key events route back to grid. */
void nitty_input_clear_terminal_mode(void);

/* ── Scroll key shortcuts (v0.3.4) ───────────────────────────────────── */

/**
 * Poll for a pending scroll shortcut event.
 * Returns: 0=none, 1=page_up, 2=page_down, 3=top, 4=bottom.
 * Clears the event after returning it.
 * Shift+PageUp/PageDown/Home/End are consumed before PTY routing.
 */
int64_t nitty_gtk4_scroll_event_poll(void);

/* ── Tab key shortcuts (v0.4.1) ──────────────────────────────────────── */

/**
 * Poll for a pending tab keyboard event.
 * Returns: 0=none, 1=new, 2=close, 3=prev, 4=next, 5..13=direct(idx 0..8),
 *          14=move_left, 15=move_right.
 * Clears the event after returning it.
 * Ctrl+Shift+T/W, Ctrl+PgUp/PgDn/1-9, and Ctrl+Shift+Left/Right
 * are all consumed before PTY routing.
 */
int64_t nitty_gtk4_tab_event_poll(void);

/* ── Tab drag-and-drop (v0.4.2) ──────────────────────────────────────── */

/**
 * Poll for a pending tab drag event.
 * Returns: 0=none, 1=drag-in-progress (update visual feedback),
 *          2=drop-completed (execute reorder now).
 * Clears the drop event after returning 2.
 * Uses GtkGestureDrag attached to the DrawingArea.
 */
int64_t nitty_gtk4_drag_event_poll(void);

/**
 * Return the X pixel coordinate where the drag started.
 * Use with tb_hit_test to determine the source tab index.
 */
int64_t nitty_gtk4_drag_get_start_x(void);

/**
 * Return the current X pixel coordinate of the drag.
 * Use with tb_hit_test to determine the drop target index.
 * Also used to position the drop indicator line.
 */
int64_t nitty_gtk4_drag_get_current_x(void);

/* ── Split pane shortcuts (v0.5.1) ──────────────────────────────────── */

/**
 * Poll for a pending pane event.
 * Returns: 0=none, 16=split_horiz, 17=split_vert, 18=close_pane_or_tab.
 * Clears the event after returning it.
 * Ctrl+Shift+E (split horizontal), Ctrl+Shift+O (split vertical), and
 * Ctrl+Shift+W (close pane or tab) are consumed before PTY routing.
 */
int64_t nitty_gtk4_pane_event_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* NITTY_INPUT_H */
