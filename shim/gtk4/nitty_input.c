/*
 * nitty_input.c — Keyboard and mouse input implementation for Nitty
 *
 * v0.0.3: GTK4 event controllers with static event storage.
 *
 * Key event flow:
 *   on_key_pressed/released → stores keyval, keycode, modifiers, type
 *   → sets g_key_pending = 1
 *   Nitpick calls nitty_gtk4_key_poll() → returns 1 and clears g_key_pending
 *   Nitpick reads keyval/keycode/modifiers/type via getter functions
 *
 * Mouse event flow (same pattern):
 *   on_click_pressed/released, on_scroll, on_motion
 *   → stores x, y, button, dx, dy, type
 *   → sets g_mouse_pending = 1
 *   Nitpick calls nitty_gtk4_mouse_poll() → returns 1 and clears g_mouse_pending
 *
 * Note: key-pressed callback returns gboolean — return FALSE to allow
 * other handlers (like IM context) to also receive the event.
 */

#include "nitty_input.h"
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <stdint.h>
#include <stdio.h>

/* Forward declaration — defined in nitty_gtk4_shim.c */
extern void nitty_gtk4_grid_handle_key(int64_t keyval, int64_t modifiers);

/* v0.6.2: Key-consumed sentinel — set when on_key_pressed intercepts a key */
extern void nitty_gtk4_set_key_consumed(int consumed);

/* v0.1.4: Terminal mode — when set, keys go to PTY instead of grid */
static int g_terminal_mode = 0;
static int64_t g_pty_master_fd = -1;

/* v0.5.5: Swap mode flag — set by Nitpick via nitty_input_set_swap_mode() */
static int g_swap_mode_active = 0;
void nitty_input_set_swap_mode(int active) { g_swap_mode_active = active; }

/* PTY shim write functions — defined in nitty_pty_shim.c */
extern int64_t nitty_pty_write_byte(int64_t fd, int64_t byte_val);
extern int64_t nitty_pty_write_string(int64_t fd, const char *str);

/* v0.5.4: Broadcast input fd array — extra PTY fds to mirror key input to */
#define NITTY_BROADCAST_MAX 32
static int64_t g_broadcast_fds[NITTY_BROADCAST_MAX];
static int     g_broadcast_count = 0;

/* Fan-out write: primary fd + all broadcast fds */
static void _bc_write_byte(int64_t byte_val)
{
    nitty_pty_write_byte(g_pty_master_fd, byte_val);
    for (int i = 0; i < g_broadcast_count; i++) {
        if (g_broadcast_fds[i] >= 0)
            nitty_pty_write_byte(g_broadcast_fds[i], byte_val);
    }
}
static void _bc_write_string(const char *str)
{
    nitty_pty_write_string(g_pty_master_fd, str);
    for (int i = 0; i < g_broadcast_count; i++) {
        if (g_broadcast_fds[i] >= 0)
            nitty_pty_write_string(g_broadcast_fds[i], str);
    }
}

/* Broadcast management — called from nitty_gtk4_shim.c */
void nitty_input_broadcast_begin(void)
{
    g_broadcast_count = 0;
}
void nitty_input_broadcast_add_fd(int64_t fd)
{
    if (g_broadcast_count < NITTY_BROADCAST_MAX && fd >= 0)
        g_broadcast_fds[g_broadcast_count++] = fd;
}
void nitty_input_broadcast_clear(void)
{
    g_broadcast_count = 0;
}
int nitty_input_broadcast_count(void)
{
    return g_broadcast_count;
}

/* ── Keyboard event state ─────────────────────────────────────────────── */

static int      g_key_pending   = 0;
static guint    g_key_keyval    = 0;
static guint    g_key_keycode   = 0;
static guint    g_key_modifiers = 0;
static int      g_key_type      = 0;   /* 1=press, 2=release */

/* ── Mouse event state ────────────────────────────────────────────────── */

static int    g_mouse_pending   = 0;
static int    g_mouse_type      = 0;   /* 1=click, 2=release, 3=scroll, 4=motion */
static double g_mouse_x         = 0.0;
static double g_mouse_y         = 0.0;
static int    g_mouse_button    = 0;
static double g_mouse_scroll_dx = 0.0;
static double g_mouse_scroll_dy = 0.0;

/* v0.3.4: Scroll shortcut event: 0=none 1=pgup 2=pgdown 3=top 4=bottom */
static int g_scroll_event = 0;

/* v0.4.2: Tab event queue: 0=none 1=new 2=close 3=prev 4=next
 * 5..13=direct(idx 0..8) 14=move_left 15=move_right */
static int g_tab_event = 0;

/* v0.5.1: Pane event queue:
 *   0=none 16=split_horiz 17=split_vert 18=close_pane_or_tab
 *   19=resize_up 20=resize_down (Ctrl+Shift+Up/Down)
 *   21=nav_left 22=nav_right 23=nav_up 24=nav_down (Alt+Arrow)
 *   25=cycle_next 26=cycle_prev (Ctrl+Tab / Ctrl+Shift+Tab)
 */
static int g_pane_event = 0;

/* v0.4.2: Tab drag-and-drop state */
static int    g_drag_active    = 0;  /* 1 while dragging */
static int    g_drag_src_idx   = -1; /* source tab index */
static double g_drag_start_x   = 0.0;
static double g_drag_start_y   = 0.0;
static double g_drag_current_x = 0.0;
static int    g_drag_drop_event = 0; /* 1 when drop just occurred */

/* ── Keyboard callbacks ───────────────────────────────────────────────── */

static gboolean on_key_pressed(GtkEventControllerKey *controller,
                                guint keyval,
                                guint keycode,
                                GdkModifierType state,
                                gpointer user_data)
{
    (void)controller;
    (void)user_data;

    g_key_keyval    = keyval;
    g_key_keycode   = keycode;
    g_key_modifiers = (guint)state;
    g_key_type      = 1;
    g_key_pending   = 1;

    /* v0.6.2: reset consumed flag at start of each key event */
    nitty_gtk4_set_key_consumed(0);

    /* v0.3.4: Intercept Shift+scroll-navigation shortcuts before PTY routing */
    if (state & GDK_SHIFT_MASK) {
        switch (keyval) {
            case GDK_KEY_Page_Up:   g_scroll_event = 1; nitty_gtk4_set_key_consumed(1); return TRUE;
            case GDK_KEY_Page_Down: g_scroll_event = 2; nitty_gtk4_set_key_consumed(1); return TRUE;
            case GDK_KEY_Home:      g_scroll_event = 3; nitty_gtk4_set_key_consumed(1); return TRUE;
            case GDK_KEY_End:       g_scroll_event = 4; nitty_gtk4_set_key_consumed(1); return TRUE;
            default: break;
        }
    }

    /* v0.4.2: Tab event queue: 0=none 1=new 2=close 3=prev 4=next
     * 5..13=direct(idx 0..8) 14=move_left 15=move_right */

    /* v0.4.1: Intercept Ctrl+Shift tab shortcuts before PTY routing */
    /* v0.5.1: Added E=split_horiz, O=split_vert; W now emits pane_event=18 */
    /* v0.5.2: Added Up=resize_up(19), Down=resize_down(20) */
    if ((state & GDK_CONTROL_MASK) && (state & GDK_SHIFT_MASK)) {
        switch (keyval) {
            case GDK_KEY_t: case GDK_KEY_T:
                g_tab_event = 1; nitty_gtk4_set_key_consumed(1); return TRUE;  /* new tab */
            case GDK_KEY_w: case GDK_KEY_W:
                /* v0.5.1: pane_event=18 lets Nitpick decide: close pane or tab */
                g_pane_event = 18; nitty_gtk4_set_key_consumed(1); return TRUE;
            case GDK_KEY_Left:  case GDK_KEY_KP_Left:
                g_tab_event = 14; nitty_gtk4_set_key_consumed(1); return TRUE;
            case GDK_KEY_Right: case GDK_KEY_KP_Right:
                g_tab_event = 15; nitty_gtk4_set_key_consumed(1); return TRUE;
            case GDK_KEY_Up:    case GDK_KEY_KP_Up:
                g_pane_event = 19; nitty_gtk4_set_key_consumed(1); return TRUE;
            case GDK_KEY_Down:  case GDK_KEY_KP_Down:
                g_pane_event = 20; nitty_gtk4_set_key_consumed(1); return TRUE;
            case GDK_KEY_e: case GDK_KEY_E:
                g_pane_event = 16; nitty_gtk4_set_key_consumed(1); return TRUE;
            case GDK_KEY_o: case GDK_KEY_O:
                g_pane_event = 17; nitty_gtk4_set_key_consumed(1); return TRUE;
            case GDK_KEY_m: case GDK_KEY_M:
                g_pane_event = 27; nitty_gtk4_set_key_consumed(1); return TRUE;
            case GDK_KEY_b: case GDK_KEY_B:
                g_pane_event = 28; nitty_gtk4_set_key_consumed(1); return TRUE;
            case GDK_KEY_s: case GDK_KEY_S:
                g_pane_event = 29; nitty_gtk4_set_key_consumed(1); return TRUE;
            case GDK_KEY_exclam:
                g_pane_event = 31; nitty_gtk4_set_key_consumed(1); return TRUE;
            case GDK_KEY_at:
                g_pane_event = 32; nitty_gtk4_set_key_consumed(1); return TRUE;
            default: break;
        }
    }

    /* v0.5.5: Escape cancels swap mode */
    if (keyval == GDK_KEY_Escape && g_swap_mode_active) {
        g_pane_event = 30; nitty_gtk4_set_key_consumed(1); return TRUE; /* swap_cancel */
    }

    /* v0.5.2: Intercept Alt+Arrow for pane focus navigation before PTY routing */
    if ((state & GDK_ALT_MASK) && !(state & GDK_CONTROL_MASK) && !(state & GDK_SHIFT_MASK)) {
        switch (keyval) {
            case GDK_KEY_Left:  case GDK_KEY_KP_Left:
                g_pane_event = 21; nitty_gtk4_set_key_consumed(1); return TRUE;
            case GDK_KEY_Right: case GDK_KEY_KP_Right:
                g_pane_event = 22; nitty_gtk4_set_key_consumed(1); return TRUE;
            case GDK_KEY_Up:    case GDK_KEY_KP_Up:
                g_pane_event = 23; nitty_gtk4_set_key_consumed(1); return TRUE;
            case GDK_KEY_Down:  case GDK_KEY_KP_Down:
                g_pane_event = 24; nitty_gtk4_set_key_consumed(1); return TRUE;
            default: break;
        }
    }

    /* v0.4.1: Intercept Ctrl+PgUp/PgDn tab switching before PTY routing */
    /* v0.5.2: Added Ctrl+Tab (25) / Ctrl+Shift+Tab (26) for pane cycling */
    if (state & GDK_CONTROL_MASK) {
        switch (keyval) {
            case GDK_KEY_Page_Up:   g_tab_event = 3; nitty_gtk4_set_key_consumed(1); return TRUE;
            case GDK_KEY_Page_Down: g_tab_event = 4; nitty_gtk4_set_key_consumed(1); return TRUE;
            case GDK_KEY_1: g_tab_event = 5;  nitty_gtk4_set_key_consumed(1); return TRUE;
            case GDK_KEY_2: g_tab_event = 6;  nitty_gtk4_set_key_consumed(1); return TRUE;
            case GDK_KEY_3: g_tab_event = 7;  nitty_gtk4_set_key_consumed(1); return TRUE;
            case GDK_KEY_4: g_tab_event = 8;  nitty_gtk4_set_key_consumed(1); return TRUE;
            case GDK_KEY_5: g_tab_event = 9;  nitty_gtk4_set_key_consumed(1); return TRUE;
            case GDK_KEY_6: g_tab_event = 10; nitty_gtk4_set_key_consumed(1); return TRUE;
            case GDK_KEY_7: g_tab_event = 11; nitty_gtk4_set_key_consumed(1); return TRUE;
            case GDK_KEY_8: g_tab_event = 12; nitty_gtk4_set_key_consumed(1); return TRUE;
            case GDK_KEY_9: g_tab_event = 13; nitty_gtk4_set_key_consumed(1); return TRUE;
            case GDK_KEY_Tab: case GDK_KEY_ISO_Left_Tab:
                if (state & GDK_SHIFT_MASK) {
                    g_pane_event = 26;
                } else {
                    g_pane_event = 25;
                }
                nitty_gtk4_set_key_consumed(1); return TRUE;
            default: break;
        }
    }

    /* Debug: print key event to stdout (only in non-terminal mode) */
    if (!g_terminal_mode) {
        const char *mod_str = "";
        if ((state & GDK_CONTROL_MASK) && (state & GDK_SHIFT_MASK))  mod_str = "Ctrl+Shift+";
        else if (state & GDK_CONTROL_MASK)                            mod_str = "Ctrl+";
        else if (state & GDK_SHIFT_MASK)                              mod_str = "Shift+";
        else if (state & GDK_ALT_MASK)                                mod_str = "Alt+";
        fprintf(stdout, "KEY PRESS: keyval=0x%04x keycode=%u mods=0x%08x  [%s0x%04x]\n",
                keyval, keycode, (unsigned)state, mod_str, keyval);
        fflush(stdout);
    }

    /* Route key: terminal mode → PTY, otherwise → grid */
    if (g_terminal_mode && g_pty_master_fd >= 0) {
        GdkModifierType mods = state;
        int has_ctrl = (mods & GDK_CONTROL_MASK) != 0;
        int has_alt  = (mods & GDK_ALT_MASK) != 0;

        /* Ctrl+letter: send control character */
        if (has_ctrl && keyval >= 'a' && keyval <= 'z') {
            _bc_write_byte((int64_t)(keyval - 'a' + 1));
            return TRUE;
        }
        if (has_ctrl && keyval >= 'A' && keyval <= 'Z') {
            _bc_write_byte((int64_t)(keyval - 'A' + 1));
            return TRUE;
        }

        /* Special keys */
        switch (keyval) {
            case GDK_KEY_Return:    _bc_write_string("\r");     return TRUE;
            case GDK_KEY_BackSpace: _bc_write_byte(0x7F);       return TRUE;
            case GDK_KEY_Tab:       _bc_write_byte(0x09);       return TRUE;
            case GDK_KEY_Escape:    _bc_write_byte(0x1B);       return TRUE;
            case GDK_KEY_Up:        _bc_write_string("\x1b[A"); return TRUE;
            case GDK_KEY_Down:      _bc_write_string("\x1b[B"); return TRUE;
            case GDK_KEY_Right:     _bc_write_string("\x1b[C"); return TRUE;
            case GDK_KEY_Left:      _bc_write_string("\x1b[D"); return TRUE;
            case GDK_KEY_Home:      _bc_write_string("\x1b[H"); return TRUE;
            case GDK_KEY_End:       _bc_write_string("\x1b[F"); return TRUE;
            case GDK_KEY_Insert:    _bc_write_string("\x1b[2~"); return TRUE;
            case GDK_KEY_Delete:    _bc_write_string("\x1b[3~"); return TRUE;
            case GDK_KEY_Page_Up:   _bc_write_string("\x1b[5~"); return TRUE;
            case GDK_KEY_Page_Down: _bc_write_string("\x1b[6~"); return TRUE;
            case GDK_KEY_F1:        _bc_write_string("\x1bOP");  return TRUE;
            case GDK_KEY_F2:        _bc_write_string("\x1bOQ");  return TRUE;
            case GDK_KEY_F3:        _bc_write_string("\x1bOR");  return TRUE;
            case GDK_KEY_F4:        _bc_write_string("\x1bOS");  return TRUE;
            case GDK_KEY_F5:        _bc_write_string("\x1b[15~"); return TRUE;
            case GDK_KEY_F6:        _bc_write_string("\x1b[17~"); return TRUE;
            case GDK_KEY_F7:        _bc_write_string("\x1b[18~"); return TRUE;
            case GDK_KEY_F8:        _bc_write_string("\x1b[19~"); return TRUE;
            case GDK_KEY_F9:        _bc_write_string("\x1b[20~"); return TRUE;
            case GDK_KEY_F10:       _bc_write_string("\x1b[21~"); return TRUE;
            case GDK_KEY_F11:       _bc_write_string("\x1b[23~"); return TRUE;
            case GDK_KEY_F12:       _bc_write_string("\x1b[24~"); return TRUE;
            default: break;
        }

        /* Alt+key: prepend ESC */
        if (has_alt && keyval >= 0x20 && keyval <= 0x7E) {
            _bc_write_byte(0x1B);
            _bc_write_byte((int64_t)keyval);
            return TRUE;
        }

        /* Printable ASCII */
        if (keyval >= 0x20 && keyval <= 0x7E) {
            _bc_write_byte((int64_t)keyval);
            return TRUE;
        }

        return FALSE;  /* Unhandled key */
    }

    /* Non-terminal mode: route to grid */
    nitty_gtk4_grid_handle_key((int64_t)keyval, (int64_t)state);

    return FALSE;
}

static void on_key_released(GtkEventControllerKey *controller,
                              guint keyval,
                              guint keycode,
                              GdkModifierType state,
                              gpointer user_data)
{
    (void)controller;
    (void)user_data;

    g_key_keyval    = keyval;
    g_key_keycode   = keycode;
    g_key_modifiers = (guint)state;
    g_key_type      = 2;
    g_key_pending   = 1;
}

/* ── Mouse callbacks ──────────────────────────────────────────────────── */

static void on_click_pressed(GtkGestureClick *gesture,
                               gint n_press,
                               gdouble x,
                               gdouble y,
                               gpointer user_data)
{
    (void)gesture;
    (void)n_press;
    (void)user_data;

    g_mouse_type    = 1;
    g_mouse_x       = x;
    g_mouse_y       = y;
    g_mouse_button  = (int)gtk_gesture_single_get_current_button(
                          GTK_GESTURE_SINGLE(gesture));
    g_mouse_pending = 1;

    fprintf(stdout, "MOUSE CLICK: button=%d x=%.1f y=%.1f\n",
            g_mouse_button, x, y);
    fflush(stdout);
}

static void on_click_released(GtkGestureClick *gesture,
                                gint n_press,
                                gdouble x,
                                gdouble y,
                                gpointer user_data)
{
    (void)gesture;
    (void)n_press;
    (void)user_data;

    g_mouse_type    = 2;
    g_mouse_x       = x;
    g_mouse_y       = y;
    g_mouse_button  = (int)gtk_gesture_single_get_current_button(
                          GTK_GESTURE_SINGLE(gesture));
    g_mouse_pending = 1;
}

static gboolean on_scroll(GtkEventControllerScroll *controller,
                           gdouble dx,
                           gdouble dy,
                           gpointer user_data)
{
    (void)controller;
    (void)user_data;

    g_mouse_type      = 3;
    g_mouse_scroll_dx = dx;
    g_mouse_scroll_dy = dy;
    g_mouse_pending   = 1;

    fprintf(stdout, "MOUSE SCROLL: dx=%.3f dy=%.3f\n", dx, dy);
    fflush(stdout);

    return FALSE;
}

static void on_motion(GtkEventControllerMotion *controller,
                       gdouble x,
                       gdouble y,
                       gpointer user_data)
{
    (void)controller;
    (void)user_data;

    g_mouse_type    = 4;
    g_mouse_x       = x;
    g_mouse_y       = y;
    g_mouse_pending = 1;
}

/* Forward declaration — nitty_render.c exposes tab bar height */
extern int64_t nitty_render_get_tab_bar_height(void);
extern int64_t nitty_gtk4_get_draw_width(void);

/* ── Tab drag-and-drop gesture callbacks (v0.4.2) ────────────────────── */

static void on_drag_begin(GtkGestureDrag *gesture, double x, double y,
                          gpointer user_data)
{
    (void)gesture; (void)user_data;
    int64_t tab_bar_h = nitty_render_get_tab_bar_height();
    /* Only initiate drag if press was inside the tab bar */
    if (tab_bar_h <= 0 || y >= (double)tab_bar_h) return;

    /* Compute which tab was pressed */
    int64_t win_w = nitty_gtk4_get_draw_width();
    if (win_w <= 0) return;
    /* tab_w calculation must match tab_bar.npk's tb_render logic */
    /* (window_width / count), floor at TB_MIN_TAB_W=120) */
    /* We don't know count here — store start_x and let Nitpick decode it */
    g_drag_start_x   = x;
    g_drag_start_y   = y;
    g_drag_current_x = x;
    g_drag_active    = 1;
    g_drag_drop_event = 0;
    /* src_idx is -1 until Nitpick reports the index via tb_hit_test */
    g_drag_src_idx   = -1;
}

static void on_drag_update(GtkGestureDrag *gesture, double offset_x,
                           double offset_y, gpointer user_data)
{
    (void)offset_y; (void)user_data;
    if (!g_drag_active) return;
    double start_x = 0.0, start_y = 0.0;
    gtk_gesture_drag_get_start_point(gesture, &start_x, &start_y);
    g_drag_current_x = start_x + offset_x;
}

static void on_drag_end(GtkGestureDrag *gesture, double offset_x,
                        double offset_y, gpointer user_data)
{
    (void)offset_y; (void)user_data;
    if (!g_drag_active) return;

    double start_x = 0.0, start_y = 0.0;
    gtk_gesture_drag_get_start_point(gesture, &start_x, &start_y);
    g_drag_current_x = start_x + offset_x;

    /* Only fire a drop event if the drag moved meaningfully */
    double dist = g_drag_current_x - g_drag_start_x;
    if (dist < 0.0) dist = -dist;
    if (dist > 8.0) {
        g_drag_drop_event = 1;
    }
    g_drag_active = 0;
}

/* ── Controller setup ─────────────────────────────────────────────────── */

int64_t nitty_gtk4_key_controller_new(int64_t widget_ptr)
{
    if (widget_ptr == 0) return 0;
    GtkWidget *widget = (GtkWidget *)(uintptr_t)widget_ptr;

    GtkEventController *controller = gtk_event_controller_key_new();
    if (controller == NULL) return 0;

    g_signal_connect(controller, "key-pressed",
                     G_CALLBACK(on_key_pressed), NULL);
    g_signal_connect(controller, "key-released",
                     G_CALLBACK(on_key_released), NULL);

    gtk_widget_add_controller(widget, controller);
    return (int64_t)(uintptr_t)controller;
}

int64_t nitty_gtk4_mouse_controllers_new(int64_t widget_ptr)
{
    if (widget_ptr == 0) return 0;
    GtkWidget *widget = (GtkWidget *)(uintptr_t)widget_ptr;

    /* Click gesture */
    GtkGesture *click = gtk_gesture_click_new();
    if (click != NULL) {
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 0); /* All buttons */
        g_signal_connect(click, "pressed",  G_CALLBACK(on_click_pressed),  NULL);
        g_signal_connect(click, "released", G_CALLBACK(on_click_released), NULL);
        gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(click));
    }

    /* Scroll controller */
    GtkEventController *scroll = gtk_event_controller_scroll_new(
        GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES
    );
    if (scroll != NULL) {
        g_signal_connect(scroll, "scroll", G_CALLBACK(on_scroll), NULL);
        gtk_widget_add_controller(widget, scroll);
    }

    /* Motion controller */
    GtkEventController *motion = gtk_event_controller_motion_new();
    if (motion != NULL) {
        g_signal_connect(motion, "motion", G_CALLBACK(on_motion), NULL);
        gtk_widget_add_controller(widget, motion);
    }

    /* v0.4.2: Drag gesture for tab reordering */
    GtkGesture *drag = gtk_gesture_drag_new();
    if (drag != NULL) {
        g_signal_connect(drag, "drag-begin",  G_CALLBACK(on_drag_begin),  NULL);
        g_signal_connect(drag, "drag-update", G_CALLBACK(on_drag_update), NULL);
        g_signal_connect(drag, "drag-end",    G_CALLBACK(on_drag_end),    NULL);
        gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(drag));
    }

    return 1;   /* Non-zero = success */
}

/* ── Keyboard polling ─────────────────────────────────────────────────── */

int64_t nitty_gtk4_key_poll(void)
{
    if (g_key_pending) {
        g_key_pending = 0;
        return 1;
    }
    return 0;
}

int64_t nitty_gtk4_key_get_keyval(void)    { return (int64_t)g_key_keyval; }
int64_t nitty_gtk4_key_get_keycode(void)   { return (int64_t)g_key_keycode; }
int64_t nitty_gtk4_key_get_modifiers(void) { return (int64_t)g_key_modifiers; }
int64_t nitty_gtk4_key_get_type(void)      { return (int64_t)g_key_type; }

/* ── GDK keyval constants ─────────────────────────────────────────────── */

int64_t nitty_gdk_key_return(void)    { return (int64_t)GDK_KEY_Return; }
int64_t nitty_gdk_key_escape(void)    { return (int64_t)GDK_KEY_Escape; }
int64_t nitty_gdk_key_backspace(void) { return (int64_t)GDK_KEY_BackSpace; }
int64_t nitty_gdk_key_tab(void)       { return (int64_t)GDK_KEY_Tab; }
int64_t nitty_gdk_key_space(void)     { return (int64_t)GDK_KEY_space; }
int64_t nitty_gdk_key_up(void)        { return (int64_t)GDK_KEY_Up; }
int64_t nitty_gdk_key_down(void)      { return (int64_t)GDK_KEY_Down; }
int64_t nitty_gdk_key_left(void)      { return (int64_t)GDK_KEY_Left; }
int64_t nitty_gdk_key_right(void)     { return (int64_t)GDK_KEY_Right; }
int64_t nitty_gdk_key_home(void)      { return (int64_t)GDK_KEY_Home; }
int64_t nitty_gdk_key_end(void)       { return (int64_t)GDK_KEY_End; }
int64_t nitty_gdk_key_page_up(void)   { return (int64_t)GDK_KEY_Page_Up; }
int64_t nitty_gdk_key_page_down(void) { return (int64_t)GDK_KEY_Page_Down; }
int64_t nitty_gdk_key_insert(void)    { return (int64_t)GDK_KEY_Insert; }
int64_t nitty_gdk_key_delete(void)    { return (int64_t)GDK_KEY_Delete; }
int64_t nitty_gdk_key_f1(void)        { return (int64_t)GDK_KEY_F1; }
int64_t nitty_gdk_key_f2(void)        { return (int64_t)GDK_KEY_F2; }
int64_t nitty_gdk_key_f3(void)        { return (int64_t)GDK_KEY_F3; }
int64_t nitty_gdk_key_f4(void)        { return (int64_t)GDK_KEY_F4; }
int64_t nitty_gdk_key_f5(void)        { return (int64_t)GDK_KEY_F5; }
int64_t nitty_gdk_key_f6(void)        { return (int64_t)GDK_KEY_F6; }
int64_t nitty_gdk_key_f7(void)        { return (int64_t)GDK_KEY_F7; }
int64_t nitty_gdk_key_f8(void)        { return (int64_t)GDK_KEY_F8; }
int64_t nitty_gdk_key_f9(void)        { return (int64_t)GDK_KEY_F9; }
int64_t nitty_gdk_key_f10(void)       { return (int64_t)GDK_KEY_F10; }
int64_t nitty_gdk_key_f11(void)       { return (int64_t)GDK_KEY_F11; }
int64_t nitty_gdk_key_f12(void)       { return (int64_t)GDK_KEY_F12; }

/* ── GDK modifier constants ───────────────────────────────────────────── */

int64_t nitty_gdk_mod_shift(void) { return (int64_t)GDK_SHIFT_MASK; }
int64_t nitty_gdk_mod_ctrl(void)  { return (int64_t)GDK_CONTROL_MASK; }
int64_t nitty_gdk_mod_alt(void)   { return (int64_t)GDK_ALT_MASK; }
int64_t nitty_gdk_mod_super(void) { return (int64_t)GDK_SUPER_MASK; }

/* ── Mouse polling ────────────────────────────────────────────────────── */

int64_t nitty_gtk4_mouse_poll(void)
{
    if (g_mouse_pending) {
        g_mouse_pending = 0;
        return 1;
    }
    return 0;
}

int64_t nitty_gtk4_mouse_get_type(void)
{
    return (int64_t)g_mouse_type;
}

int64_t nitty_gtk4_mouse_get_x(void)
{
    return (int64_t)(g_mouse_x * 1000.0);
}

int64_t nitty_gtk4_mouse_get_y(void)
{
    return (int64_t)(g_mouse_y * 1000.0);
}

int64_t nitty_gtk4_mouse_get_button(void)
{
    return (int64_t)g_mouse_button;
}

int64_t nitty_gtk4_mouse_get_scroll_dx(void)
{
    return (int64_t)(g_mouse_scroll_dx * 1000.0);
}

int64_t nitty_gtk4_mouse_get_scroll_dy(void)
{
    return (int64_t)(g_mouse_scroll_dy * 1000.0);
}

/* ── Terminal mode (v0.1.4) ───────────────────────────────────────────── */

void nitty_input_set_terminal_mode(int64_t master_fd)
{
    g_terminal_mode   = 1;
    g_pty_master_fd   = master_fd;
    fprintf(stdout, "Input: terminal mode enabled (fd=%d)\n", (int)master_fd);
    fflush(stdout);
}

void nitty_input_clear_terminal_mode(void)
{
    g_terminal_mode   = 0;
    g_pty_master_fd   = -1;
}

/* ── v0.3.4: Scroll shortcut polling ─────────────────────────────────── */

int64_t nitty_gtk4_scroll_event_poll(void)
{
    if (g_scroll_event != 0) {
        int64_t ev = (int64_t)g_scroll_event;
        g_scroll_event = 0;
        return ev;
    }
    return 0;
}

/* ── v0.4.1: Tab event polling ────────────────────────────────────────── */
/* Returns: 0=none 1=new 3=prev 4=next 5..13=direct(idx 0..8)            */
/* v0.4.2: 14=move_left 15=move_right                                     */
/* Note: code 2 (close tab) is now handled via nitty_gtk4_pane_event_poll */
/*       (code 18=close_pane_or_tab) so Nitpick can decide per pane count */

int64_t nitty_gtk4_tab_event_poll(void)
{
    if (g_tab_event != 0) {
        int64_t ev = (int64_t)g_tab_event;
        g_tab_event = 0;
        return ev;
    }
    return 0;
}

/* ── v0.5.1: Pane event polling ───────────────────────────────────────── */
/* Returns: 0=none 16=split_horiz 17=split_vert 18=close_pane_or_tab     */

int64_t nitty_gtk4_pane_event_poll(void)
{
    if (g_pane_event != 0) {
        int64_t ev = (int64_t)g_pane_event;
        g_pane_event = 0;
        return ev;
    }
    return 0;
}

/* ── v0.4.2: Tab drag-and-drop polling ──────────────────────────────── */
/* Returns: 0=none 1=dragging (visual feedback) 2=drop (reorder now)      */

int64_t nitty_gtk4_drag_event_poll(void)
{
    if (g_drag_drop_event) {
        g_drag_drop_event = 0;
        return 2;  /* drop completed — Nitpick should execute the reorder */
    }
    if (g_drag_active) {
        return 1;  /* drag in progress — Nitpick should show visual feedback */
    }
    return 0;
}

/* Source tab's x position at drag start (pixel, integer) */
int64_t nitty_gtk4_drag_get_start_x(void)
{
    return (int64_t)g_drag_start_x;
}

/* Current drag x position (pixel, integer) — for drop indicator placement */
int64_t nitty_gtk4_drag_get_current_x(void)
{
    return (int64_t)g_drag_current_x;
}
