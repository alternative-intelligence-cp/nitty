/*
 * nitty_gtk4_shim.c — GTK4 C FFI shim implementation for Nitty
 *
 * Provides a simplified, opaque-pointer API over GTK4 for Nitpick code.
 * All pointer values are passed as int64_t between Nitpick and C.
 *
 * v0.0.1 architecture:
 *   - The shim manages the window internally via a stored static pointer.
 *   - Nitpick configures title, size, and app_id BEFORE calling app_run().
 *   - The internal on_activate() handler creates and shows the window.
 *   - After app_run() returns, Nitpick calls app_free() to clean up.
 *
 * This design avoids Nitpick function-pointer FFI complexity in v0.0.1.
 * In v0.1.x+, a full callback registration mechanism will be added.
 */

#include "nitty_gtk4_shim.h"
#include <gtk/gtk.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

/* ── Internal state ──────────────────────────────────────────────────── */

/* Configuration set by Nitpick before calling app_run() */
static char   g_window_title[256] = "Nitty Terminal";
static int    g_window_width      = 1024;
static int    g_window_height     = 768;

/* The main window created by on_activate() */
static GtkWidget *g_main_window = NULL;

/* ── Internal GTK signal handlers ────────────────────────────────────── */

/*
 * on_activate — called by GTK when the application is first activated.
 * Creates the main window with the pre-configured title and size.
 */
static void on_activate(GtkApplication *app, gpointer user_data)
{
    (void)user_data;

    GtkWidget *window = gtk_application_window_new(app);
    if (window == NULL) {
        return;
    }

    gtk_window_set_title(GTK_WINDOW(window), g_window_title);
    gtk_window_set_default_size(GTK_WINDOW(window), g_window_width, g_window_height);

    g_main_window = window;

    gtk_window_present(GTK_WINDOW(window));
}

/* ── Configuration (must be called before app_run) ───────────────────── */

void nitty_gtk4_configure_window(const char *title, int64_t width, int64_t height)
{
    if (title != NULL) {
        strncpy(g_window_title, title, sizeof(g_window_title) - 1);
        g_window_title[sizeof(g_window_title) - 1] = '\0';
    }
    if (width > 0)  g_window_width  = (int)width;
    if (height > 0) g_window_height = (int)height;
}

/* ── Application lifecycle ────────────────────────────────────────────── */

int64_t nitty_gtk4_app_new(const char *app_id)
{
    if (app_id == NULL) {
        return 0;
    }

    GtkApplication *app = gtk_application_new(app_id, G_APPLICATION_DEFAULT_FLAGS);
    if (app == NULL) {
        return 0;
    }

    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);

    return (int64_t)(uintptr_t)app;
}

int64_t nitty_gtk4_app_run(int64_t app_ptr)
{
    if (app_ptr == 0) {
        return -1;
    }

    GtkApplication *app = (GtkApplication *)(uintptr_t)app_ptr;
    int status = g_application_run(G_APPLICATION(app), 0, NULL);
    return (int64_t)status;
}

void nitty_gtk4_app_quit(int64_t app_ptr)
{
    if (app_ptr == 0) {
        return;
    }
    g_application_quit((GApplication *)(uintptr_t)app_ptr);
}

void nitty_gtk4_app_free(int64_t app_ptr)
{
    if (app_ptr == 0) {
        return;
    }
    g_object_unref((GObject *)(uintptr_t)app_ptr);
    g_main_window = NULL;
}

/* ── Window access (after app_run) ───────────────────────────────────── */

int64_t nitty_gtk4_get_main_window(void)
{
    return (int64_t)(uintptr_t)g_main_window;
}

/* ── Window management ────────────────────────────────────────────────── */

int64_t nitty_gtk4_window_new(int64_t app_ptr)
{
    if (app_ptr == 0) {
        return 0;
    }
    GtkWidget *window = gtk_application_window_new(
        (GtkApplication *)(uintptr_t)app_ptr
    );
    return (int64_t)(uintptr_t)window;
}

void nitty_gtk4_window_set_title(int64_t win_ptr, const char *title)
{
    if (win_ptr == 0 || title == NULL) return;
    gtk_window_set_title((GtkWindow *)(uintptr_t)win_ptr, title);
}

void nitty_gtk4_window_set_size(int64_t win_ptr, int64_t width, int64_t height)
{
    if (win_ptr == 0 || width <= 0 || height <= 0) return;
    gtk_window_set_default_size(
        (GtkWindow *)(uintptr_t)win_ptr, (int)width, (int)height
    );
}

void nitty_gtk4_window_show(int64_t win_ptr)
{
    if (win_ptr == 0) return;
    gtk_window_present((GtkWindow *)(uintptr_t)win_ptr);
}

void nitty_gtk4_window_close(int64_t win_ptr)
{
    if (win_ptr == 0) return;
    gtk_window_close((GtkWindow *)(uintptr_t)win_ptr);
}

int64_t nitty_gtk4_window_get_width(int64_t win_ptr)
{
    if (win_ptr == 0) return -1;
    return (int64_t)gtk_widget_get_width((GtkWidget *)(uintptr_t)win_ptr);
}

int64_t nitty_gtk4_window_get_height(int64_t win_ptr)
{
    if (win_ptr == 0) return -1;
    return (int64_t)gtk_widget_get_height((GtkWidget *)(uintptr_t)win_ptr);
}

/* ── Callback registration (stub — implemented in v0.1.x) ───────────── */

void nitty_gtk4_set_activate_callback(int64_t app_ptr, void (*callback)(int64_t))
{
    /* Reserved for v0.1.x full callback dispatch implementation */
    (void)app_ptr;
    (void)callback;
}
