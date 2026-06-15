/*
 * nitty_gtk4_shim.c — GTK4 C FFI shim implementation for Nitty
 *
 * v0.0.2: Added DrawingArea with cairo draw callback, Cairo drawing
 *         primitives (fixed-point), Pango text rendering, and HiDPI support.
 *
 * Draw architecture:
 *   1. nitty_gtk4_drawing_area_set_draw_func() registers on_draw_func()
 *   2. When GTK needs to paint, on_draw_func() fires:
 *      a. Stores cairo_t*, width, height in statics
 *      b. Calls the registered render callback (g_render_callback)
 *      c. The render callback uses nitty_cairo_* / nitty_pango_* to paint
 *      d. Clears the statics when done
 *   3. Nitpick's render function is called indirectly through the C callback
 *      or directly via the pre-configure pattern (v0.0.2 uses pre-configure).
 */

#include "nitty_gtk4_shim.h"
#include "nitty_render.h"
#include "nitty_input.h"
#include "nitty_grid.h"
#include <gtk/gtk.h>
#include <pango/pangocairo.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

/* ═══════════════════════════════════════════════════════════════════════
 * v0.0.1: Application lifecycle + Window management (unchanged)
 * ═══════════════════════════════════════════════════════════════════════ */

/* Window configuration */
static char   g_window_title[256] = "Nitty Terminal";
static int    g_window_width      = 1024;
static int    g_window_height     = 768;

/* Main window created by on_activate */
static GtkWidget *g_main_window  = NULL;

/* DrawingArea for the terminal grid */
static GtkWidget *g_drawing_area   = NULL;
static int        g_use_drawing_area = 0;  /* Set to 1 by nitty_gtk4_drawing_area_new */
static int        g_use_input        = 0;  /* Set to 1 by nitty_gtk4_input_enable */
static int        g_use_grid         = 0;  /* Set to 1 by nitty_gtk4_grid_enable */
static char       g_grid_font[256]   = "Monospace 12";

/* Resize tracking */
static int        g_resize_pending   = 0;
static int64_t    g_resize_width     = 0;
static int64_t    g_resize_height    = 0;

/* Forward declaration of on_draw_func */
static void on_draw_func(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data);

/* Cursor blink timer callback */
static gboolean on_cursor_blink(gpointer user_data)
{
    (void)user_data;
    nitty_grid_toggle_cursor_blink();
    if (g_drawing_area != NULL) {
        gtk_widget_queue_draw(g_drawing_area);
    }
    return G_SOURCE_CONTINUE;  /* Keep the timer running */
}

/* Resize callback — fires when DrawingArea size changes */
static void on_da_resize(GtkDrawingArea *area, gint width, gint height, gpointer user_data)
{
    (void)area;
    (void)user_data;
    g_resize_pending = 1;
    g_resize_width   = (int64_t)width;
    g_resize_height  = (int64_t)height;

    if (g_use_grid) {
        nitty_grid_resize(g_resize_width, g_resize_height);
    }
}

/* on_activate — creates the window with pre-configured settings.
 * If g_use_drawing_area is set, creates the DrawingArea here (GTK is now init'd). */
static void on_activate(GtkApplication *app, gpointer user_data)
{
    (void)user_data;

    GtkWidget *window = gtk_application_window_new(app);
    if (window == NULL) return;

    gtk_window_set_title(GTK_WINDOW(window), g_window_title);
    gtk_window_set_default_size(GTK_WINDOW(window), g_window_width, g_window_height);

    g_main_window = window;

    /* Create DrawingArea NOW — GTK is initialized at this point */
    if (g_use_drawing_area) {
        GtkWidget *da = gtk_drawing_area_new();
        if (da != NULL) {
            gtk_widget_set_hexpand(da, TRUE);
            gtk_widget_set_vexpand(da, TRUE);
            gtk_drawing_area_set_draw_func(
                GTK_DRAWING_AREA(da), on_draw_func, NULL, NULL
            );
            /* Connect resize signal for grid recalculation */
            g_signal_connect(da, "resize", G_CALLBACK(on_da_resize), NULL);
            gtk_window_set_child(GTK_WINDOW(window), da);
            g_drawing_area = da;
        }
    }

    /* Attach input controllers to the window */
    if (g_use_input) {
        /* Key controller on window so it receives focus events */
        nitty_gtk4_key_controller_new((int64_t)(uintptr_t)window);
        /* Mouse controllers on DrawingArea if available, else window */
        GtkWidget *input_target = (g_drawing_area != NULL) ? g_drawing_area : window;
        nitty_gtk4_mouse_controllers_new((int64_t)(uintptr_t)input_target);
    }

    /* Initialize the terminal grid if enabled */
    if (g_use_grid && g_drawing_area != NULL) {
        nitty_grid_init(
            (int64_t)g_window_width,
            (int64_t)g_window_height,
            g_grid_font
        );

        /* Cursor blink timer: toggle every 530ms */
        g_timeout_add(530, (GSourceFunc)on_cursor_blink, NULL);
    }

    gtk_window_present(GTK_WINDOW(window));
}

/* ── Configuration ────────────────────────────────────────────────────── */

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
    if (app_id == NULL) return 0;

    GtkApplication *app = gtk_application_new(app_id, G_APPLICATION_DEFAULT_FLAGS);
    if (app == NULL) return 0;

    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
    return (int64_t)(uintptr_t)app;
}

int64_t nitty_gtk4_app_run(int64_t app_ptr)
{
    if (app_ptr == 0) return -1;
    GtkApplication *app = (GtkApplication *)(uintptr_t)app_ptr;
    int status = g_application_run(G_APPLICATION(app), 0, NULL);
    return (int64_t)status;
}

void nitty_gtk4_app_quit(int64_t app_ptr)
{
    if (app_ptr == 0) return;
    g_application_quit((GApplication *)(uintptr_t)app_ptr);
}

void nitty_gtk4_app_free(int64_t app_ptr)
{
    if (app_ptr == 0) return;
    g_object_unref((GObject *)(uintptr_t)app_ptr);
    g_main_window  = NULL;
    g_drawing_area = NULL;
}

/* ── Window access ────────────────────────────────────────────────────── */

int64_t nitty_gtk4_get_main_window(void)
{
    return (int64_t)(uintptr_t)g_main_window;
}

/* ── Window management ────────────────────────────────────────────────── */

int64_t nitty_gtk4_window_new(int64_t app_ptr)
{
    if (app_ptr == 0) return 0;
    GtkWidget *w = gtk_application_window_new((GtkApplication *)(uintptr_t)app_ptr);
    return (int64_t)(uintptr_t)w;
}

void nitty_gtk4_window_set_title(int64_t win_ptr, const char *title)
{
    if (win_ptr == 0 || title == NULL) return;
    gtk_window_set_title((GtkWindow *)(uintptr_t)win_ptr, title);
}

void nitty_gtk4_window_set_size(int64_t win_ptr, int64_t width, int64_t height)
{
    if (win_ptr == 0 || width <= 0 || height <= 0) return;
    gtk_window_set_default_size((GtkWindow *)(uintptr_t)win_ptr, (int)width, (int)height);
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

void nitty_gtk4_window_set_child(int64_t win_ptr, int64_t widget_ptr)
{
    if (win_ptr == 0 || widget_ptr == 0) return;
    gtk_window_set_child(
        (GtkWindow *)(uintptr_t)win_ptr,
        (GtkWidget *)(uintptr_t)widget_ptr
    );
}

/* ── Callback stubs ───────────────────────────────────────────────────── */

void nitty_gtk4_set_activate_callback(int64_t app_ptr, void (*callback)(int64_t))
{
    (void)app_ptr;
    (void)callback;
}

/* ── Input enable ─────────────────────────────────────────────────────── */

void nitty_gtk4_input_enable(void)
{
    g_use_input = 1;
}

/* ── GTK main loop iteration (non-blocking) ───────────────────────────── */

int64_t nitty_gtk4_iteration(void)
{
    /* Process one pending event; return 1 if more events are pending */
    gboolean more = g_main_context_iteration(NULL, FALSE);
    return more ? 1 : 0;
}

/* ── Grid enable ──────────────────────────────────────────────────────── */

void nitty_gtk4_grid_enable(const char *font_desc)
{
    g_use_grid = 1;
    if (font_desc != NULL) {
        strncpy(g_grid_font, font_desc, sizeof(g_grid_font) - 1);
        g_grid_font[sizeof(g_grid_font) - 1] = '\0';
    }
}

/* ── Resize polling ───────────────────────────────────────────────────── */

int64_t nitty_gtk4_resize_poll(void)
{
    if (g_resize_pending) {
        g_resize_pending = 0;
        return 1;
    }
    return 0;
}

int64_t nitty_gtk4_resize_get_width(void)  { return g_resize_width; }
int64_t nitty_gtk4_resize_get_height(void) { return g_resize_height; }

/* ── Key-to-grid input handling (v0.0.4) ──────────────────────────────── */

/**
 * Process a key press and write it to the grid.
 * Called from the key-pressed callback when grid mode is active.
 *
 * This is NOT terminal emulation — just proof that input→grid→render works.
 * Handles: printable chars (write + advance cursor), Enter, Backspace.
 */
void nitty_gtk4_grid_handle_key(int64_t keyval, int64_t modifiers)
{
    if (!g_use_grid) return;
    (void)modifiers;

    int64_t cols = nitty_grid_get_cols();
    int64_t rows = nitty_grid_get_rows();
    int64_t cc   = nitty_grid_get_cursor_col();
    int64_t cr   = nitty_grid_get_cursor_row();

    /* Enter: move to next line */
    if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) {
        cc = 0;
        cr++;
        if (cr >= rows) cr = rows - 1;
        nitty_grid_set_cursor(cc, cr);
        if (g_drawing_area) gtk_widget_queue_draw(g_drawing_area);
        return;
    }

    /* Backspace: move left, clear cell */
    if (keyval == GDK_KEY_BackSpace) {
        if (cc > 0) {
            cc--;
        } else if (cr > 0) {
            cr--;
            cc = cols - 1;
        }
        nitty_grid_set_cell(cc, cr, 0x20, 0xCCCCCC, 0x1E1E1E);
        nitty_grid_set_cursor(cc, cr);
        if (g_drawing_area) gtk_widget_queue_draw(g_drawing_area);
        return;
    }

    /* Arrow keys */
    if (keyval == GDK_KEY_Left)  { if (cc > 0)        { nitty_grid_set_cursor(cc-1, cr); } }
    if (keyval == GDK_KEY_Right) { if (cc < cols - 1)  { nitty_grid_set_cursor(cc+1, cr); } }
    if (keyval == GDK_KEY_Up)    { if (cr > 0)         { nitty_grid_set_cursor(cc, cr-1); } }
    if (keyval == GDK_KEY_Down)  { if (cr < rows - 1)  { nitty_grid_set_cursor(cc, cr+1); } }
    if (keyval >= GDK_KEY_Left && keyval <= GDK_KEY_Down) {
        if (g_drawing_area) gtk_widget_queue_draw(g_drawing_area);
        return;
    }

    /* Printable ASCII: write character and advance cursor */
    if (keyval >= 0x20 && keyval <= 0x7E) {
        nitty_grid_set_cell(cc, cr, (int64_t)keyval, 0xCCCCCC, 0x1E1E1E);
        cc++;
        if (cc >= cols) {
            cc = 0;
            cr++;
            if (cr >= rows) cr = rows - 1;
        }
        nitty_grid_set_cursor(cc, cr);
        if (g_drawing_area) gtk_widget_queue_draw(g_drawing_area);
        return;
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 * v0.0.2: DrawingArea + Draw callback infrastructure
 * ═══════════════════════════════════════════════════════════════════════ */

/* ── Draw state (valid only inside on_draw_func) ──────────────────────── */

static cairo_t *g_draw_cr      = NULL;
static int      g_draw_width   = 0;
static int      g_draw_height  = 0;

/* Registered render callback */
static void (*g_render_callback)(void) = NULL;

/*
 * on_draw_func — GTK4 DrawingArea draw function.
 * Called by GTK when the widget needs to be painted.
 * Sets up statics so Nitpick-side (or C-side) render code can use them.
 */
static void on_draw_func(GtkDrawingArea *area,
                          cairo_t *cr,
                          int width,
                          int height,
                          gpointer user_data)
{
    (void)area;
    (void)user_data;

    g_draw_cr     = cr;
    g_draw_width  = width;
    g_draw_height = height;

    if (g_render_callback != NULL) {
        g_render_callback();
    } else if (g_use_grid) {
        /* v0.0.4: render via the terminal grid */
        nitty_grid_render((int64_t)(uintptr_t)cr, (int64_t)width, (int64_t)height);
    } else {
        /* v0.0.2 fallback: render the text fill grid */
        nitty_render_frame(cr, width, height);
    }

    g_draw_cr     = NULL;
    g_draw_width  = 0;
    g_draw_height = 0;
}

/* ── DrawingArea ──────────────────────────────────────────────────────── */

int64_t nitty_gtk4_drawing_area_new(void)
{
    /* Mark that on_activate should create the DrawingArea.
     * We can't create GTK widgets before g_application_run(). */
    g_use_drawing_area = 1;
    return 1;  /* Non-zero = success (actual pointer set in on_activate) */
}

void nitty_gtk4_drawing_area_set_size(int64_t da_ptr, int64_t w, int64_t h)
{
    if (da_ptr == 0 || w <= 0 || h <= 0) return;
    gtk_drawing_area_set_content_width(
        GTK_DRAWING_AREA((GtkWidget *)(uintptr_t)da_ptr), (int)w
    );
    gtk_drawing_area_set_content_height(
        GTK_DRAWING_AREA((GtkWidget *)(uintptr_t)da_ptr), (int)h
    );
}

void nitty_gtk4_drawing_area_set_draw_func(int64_t da_ptr)
{
    /* In v0.0.2, the draw func is set inside on_activate automatically.
     * This function is kept for API completeness. */
    (void)da_ptr;
}

void nitty_gtk4_widget_queue_draw(int64_t widget_ptr)
{
    if (widget_ptr == 0) return;
    gtk_widget_queue_draw((GtkWidget *)(uintptr_t)widget_ptr);
}

/* ── Draw state getters ───────────────────────────────────────────────── */

int64_t nitty_gtk4_get_draw_cr(void)
{
    return (int64_t)(uintptr_t)g_draw_cr;
}

int64_t nitty_gtk4_get_draw_width(void)
{
    return (int64_t)g_draw_width;
}

int64_t nitty_gtk4_get_draw_height(void)
{
    return (int64_t)g_draw_height;
}

/* ── Render callback ──────────────────────────────────────────────────── */

void nitty_gtk4_set_render_callback(void (*render_fn)(void))
{
    g_render_callback = render_fn;
}

/* ═══════════════════════════════════════════════════════════════════════
 * v0.0.2: Cairo drawing (fixed-point × 1000)
 * ═══════════════════════════════════════════════════════════════════════ */

void nitty_cairo_set_source_rgb(int64_t cr, int64_t r_fp, int64_t g_fp, int64_t b_fp)
{
    if (cr == 0) return;
    cairo_set_source_rgb(
        (cairo_t *)(uintptr_t)cr,
        (double)r_fp / 1000.0,
        (double)g_fp / 1000.0,
        (double)b_fp / 1000.0
    );
}

void nitty_cairo_paint(int64_t cr)
{
    if (cr == 0) return;
    cairo_paint((cairo_t *)(uintptr_t)cr);
}

void nitty_cairo_rectangle(int64_t cr, int64_t x_fp, int64_t y_fp, int64_t w_fp, int64_t h_fp)
{
    if (cr == 0) return;
    cairo_rectangle(
        (cairo_t *)(uintptr_t)cr,
        (double)x_fp / 1000.0,
        (double)y_fp / 1000.0,
        (double)w_fp / 1000.0,
        (double)h_fp / 1000.0
    );
}

void nitty_cairo_fill(int64_t cr)
{
    if (cr == 0) return;
    cairo_fill((cairo_t *)(uintptr_t)cr);
}

void nitty_cairo_move_to(int64_t cr, int64_t x_fp, int64_t y_fp)
{
    if (cr == 0) return;
    cairo_move_to(
        (cairo_t *)(uintptr_t)cr,
        (double)x_fp / 1000.0,
        (double)y_fp / 1000.0
    );
}

/* ═══════════════════════════════════════════════════════════════════════
 * v0.0.2: Pango text rendering
 * ═══════════════════════════════════════════════════════════════════════ */

int64_t nitty_pango_layout_new(int64_t cr)
{
    if (cr == 0) return 0;
    PangoLayout *layout = pango_cairo_create_layout((cairo_t *)(uintptr_t)cr);
    return (int64_t)(uintptr_t)layout;
}

void nitty_pango_layout_set_font(int64_t layout, const char *font_desc)
{
    if (layout == 0 || font_desc == NULL) return;

    PangoFontDescription *desc = pango_font_description_from_string(font_desc);
    if (desc != NULL) {
        pango_layout_set_font_description(
            (PangoLayout *)(uintptr_t)layout, desc
        );
        pango_font_description_free(desc);
    }
}

void nitty_pango_layout_set_text(int64_t layout, const char *text)
{
    if (layout == 0 || text == NULL) return;
    pango_layout_set_text((PangoLayout *)(uintptr_t)layout, text, -1);
}

int64_t nitty_pango_layout_get_pixel_size(int64_t layout)
{
    if (layout == 0) return 0;

    int width = 0, height = 0;
    pango_layout_get_pixel_size((PangoLayout *)(uintptr_t)layout, &width, &height);

    /* Encode as width * 65536 + height */
    return (int64_t)width * 65536 + (int64_t)height;
}

void nitty_pango_show_layout(int64_t cr, int64_t layout)
{
    if (cr == 0 || layout == 0) return;
    pango_cairo_show_layout(
        (cairo_t *)(uintptr_t)cr,
        (PangoLayout *)(uintptr_t)layout
    );
}

void nitty_pango_layout_destroy(int64_t layout)
{
    if (layout == 0) return;
    g_object_unref((GObject *)(uintptr_t)layout);
}

/* ═══════════════════════════════════════════════════════════════════════
 * v0.0.2: Scale factor
 * ═══════════════════════════════════════════════════════════════════════ */

int64_t nitty_gtk4_widget_get_scale_factor(int64_t widget_ptr)
{
    if (widget_ptr == 0) return 1;
    return (int64_t)gtk_widget_get_scale_factor((GtkWidget *)(uintptr_t)widget_ptr);
}
