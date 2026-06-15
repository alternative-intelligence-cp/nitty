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

    /* Debug: print key event to stdout */
    const char *mod_str = "";
    if ((state & GDK_CONTROL_MASK) && (state & GDK_SHIFT_MASK))  mod_str = "Ctrl+Shift+";
    else if (state & GDK_CONTROL_MASK)                            mod_str = "Ctrl+";
    else if (state & GDK_SHIFT_MASK)                              mod_str = "Shift+";
    else if (state & GDK_ALT_MASK)                                mod_str = "Alt+";
    fprintf(stdout, "KEY PRESS: keyval=0x%04x keycode=%u mods=0x%08x  [%s0x%04x]\n",
            keyval, keycode, (unsigned)state, mod_str, keyval);
    fflush(stdout);

    /* Route key to grid input handler */
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
