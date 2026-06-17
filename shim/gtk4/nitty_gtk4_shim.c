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
/* v0.7.3: X11 keep-above support */
#ifdef GDK_WINDOWING_X11
#include <gdk/x11/gdkx.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#endif

/* ═══════════════════════════════════════════════════════════════════════
 * v0.0.1: Application lifecycle + Window management (unchanged)
 * ═══════════════════════════════════════════════════════════════════════ */

/* Window configuration */
static char   g_window_title[256] = "Nitty Terminal";
static int    g_window_width      = 1024;
static int    g_window_height     = 768;
/* v0.7.3: pre-run window feature config */
static int64_t g_cfg_opacity_fp1000    = 1000;  /* 1.0 = fully opaque */
static int64_t g_cfg_default_width     = 0;     /* 0 = use g_window_width */
static int64_t g_cfg_default_height    = 0;     /* 0 = use g_window_height */

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

/* v0.1.4: Terminal mode */
static int        g_use_terminal     = 0;  /* Set by nitty_gtk4_terminal_enable */
static int64_t    g_terminal_master_fd = -1;
static int        g_terminal_shell_dead = 0;
static int64_t    g_terminal_child_pid = -1;

/* PTY shim functions */
extern int64_t nitty_pty_openpt(void);
extern int64_t nitty_pty_grantpt(int64_t master_fd);
extern int64_t nitty_pty_unlockpt(int64_t master_fd);
extern int64_t nitty_pty_spawn_shell(int64_t master_fd, int64_t rows, int64_t cols);
extern int64_t nitty_pty_set_nonblock(int64_t fd);
extern int64_t nitty_pty_set_winsize(int64_t fd, int64_t rows, int64_t cols,
                                      int64_t xpixel, int64_t ypixel);
extern int64_t nitty_pty_read_raw(int64_t fd, int64_t buf_ptr, int64_t max_len);
extern int64_t nitty_pty_child_alive(int64_t pid);
extern int64_t nitty_pty_close(int64_t fd);
extern int64_t nitty_pty_write_byte(int64_t fd, int64_t byte_val); /* v0.3.3 */

/* Input terminal mode */
extern void nitty_input_set_terminal_mode(int64_t master_fd);

/* Grid output processing */
extern int64_t nitty_grid_process_output(const char *buf, int64_t len);

/* v0.3.0: Nitpick terminal widget — called from on_draw_func each frame.
 * Declared as weak so binaries that don't include terminal_widget.npk
 * (e.g., unit tests) still link cleanly against the shim. */
__attribute__((weak)) void tw_on_draw(void) { /* no-op default */ }

/* Forward declaration of on_draw_func */
static void on_draw_func(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data);

/* Forward declaration of terminal output polling (v0.1.4) */
static void nitty_terminal_poll_output(void);

/* Cursor blink timer callback — toggles cursor blink phase each 530ms */
static gboolean on_cursor_blink(gpointer user_data)
{
    (void)user_data;
    /* v0.3.3: toggle the render-side cursor blink phase */
    int64_t phase = nitty_render_get_cursor_blink_phase();
    nitty_render_set_cursor_blink_phase(phase ? 0 : 1);
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

    /* v0.7.3: apply default size (window_state restore or configure_window) */
    int cfg_w = (g_cfg_default_width  > 0) ? (int)g_cfg_default_width  : g_window_width;
    int cfg_h = (g_cfg_default_height > 0) ? (int)g_cfg_default_height : g_window_height;
    gtk_window_set_default_size(GTK_WINDOW(window), cfg_w, cfg_h);

    /* v0.7.3: apply opacity from config (0.0..1.0 maps to fp1000 0..1000) */
    if (g_cfg_opacity_fp1000 < 1000) {
        double opacity = (double)g_cfg_opacity_fp1000 / 1000.0;
        gtk_widget_set_opacity(GTK_WIDGET(window), opacity);
    }

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

    /* v0.1.4: Spawn shell and wire PTY I/O */
    if (g_use_terminal && g_use_grid) {
        int64_t rows = nitty_grid_get_rows();
        int64_t cols = nitty_grid_get_cols();

        /* Open PTY master */
        int64_t master_fd = nitty_pty_openpt();
        if (master_fd < 0) {
            fprintf(stderr, "Terminal: ERROR — failed to open PTY master\n");
        } else {
            /* Grant and unlock */
            nitty_pty_grantpt(master_fd);
            nitty_pty_unlockpt(master_fd);

            /* Set initial window size */
            nitty_pty_set_winsize(master_fd, rows, cols, 0, 0);

            /* Set non-blocking before spawning */
            nitty_pty_set_nonblock(master_fd);

            /* Spawn shell */
            int64_t pid = nitty_pty_spawn_shell(master_fd, rows, cols);
            if (pid > 0) {
                g_terminal_master_fd = master_fd;
                g_terminal_child_pid = pid;

                /* Enable terminal mode input routing */
                nitty_input_set_terminal_mode(master_fd);

                /* Start fd watch for PTY readability */
                nitty_gtk4_add_fd_watch(master_fd);

                /* Register output polling callback (16ms ~60fps) */
                nitty_gtk4_set_idle_callback(nitty_terminal_poll_output);

                fprintf(stdout, "Terminal: shell spawned (PID %ld, master fd %ld, %ldx%ld)\n",
                        (long)pid, (long)master_fd, (long)cols, (long)rows);
            } else {
                fprintf(stderr, "Terminal: ERROR — failed to spawn shell\n");
                nitty_pty_close(master_fd);
            }
        }
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

/* ── Terminal enable (v0.1.4) ─────────────────────────────────────────── */

void nitty_gtk4_terminal_enable(void)
{
    g_use_terminal = 1;
}

/* PTY output polling — called every ~16ms from idle callback */
static char g_poll_buf[16384];

/* -- PTY byte queue (for Nitpick pipeline consumption) -------------------
 * The C-side nitty_grid_process_output is the legacy renderer path.
 * In parallel, raw PTY bytes are enqueued here so the Nitpick-side
 * pipeline (terminal_pipeline.npk) can process them each draw frame.
 *
 * Circular buffer: 65536 bytes. On overflow, oldest bytes are dropped
 * (should never happen at normal terminal speeds).
 */
#define NITTY_PTY_QUEUE_SIZE 65536
static unsigned char g_pty_queue[NITTY_PTY_QUEUE_SIZE];
static int           g_pty_queue_write = 0;
static int           g_pty_queue_read  = 0;

static void pty_queue_push(const char *buf, int64_t len)
{
    for (int64_t i = 0; i < len; i++) {
        int next_write = (g_pty_queue_write + 1) % NITTY_PTY_QUEUE_SIZE;
        if (next_write == g_pty_queue_read) {
            /* Queue full -- drop oldest byte */
            g_pty_queue_read = (g_pty_queue_read + 1) % NITTY_PTY_QUEUE_SIZE;
        }
        g_pty_queue[g_pty_queue_write] = (unsigned char)buf[i];
        g_pty_queue_write = next_write;
    }
}

/** How many bytes are pending in the PTY queue. */
int64_t nitty_gtk4_pty_bytes_available(void)
{
    int avail = (g_pty_queue_write - g_pty_queue_read + NITTY_PTY_QUEUE_SIZE) % NITTY_PTY_QUEUE_SIZE;
    return (int64_t)avail;
}

/**
 * Dequeue one byte from the PTY output queue.
 * Returns the byte value [0..255], or -1 if the queue is empty.
 */
int64_t nitty_gtk4_pty_poll_byte(void)
{
    if (g_pty_queue_read == g_pty_queue_write) return -1;
    unsigned char b = g_pty_queue[g_pty_queue_read];
    g_pty_queue_read = (g_pty_queue_read + 1) % NITTY_PTY_QUEUE_SIZE;
    return (int64_t)b;
}

/**
 * v0.3.3: Write one byte to the PTY master fd.
 * Used by Nitpick pipeline for responses (e.g., DECSET 1004 focus reporting).
 * Returns the number of bytes written (1), or -1 on error/no fd.
 */
int64_t nitty_gtk4_pty_write_byte(int64_t byte_val)
{
    if (g_terminal_master_fd < 0) return -1;
    return nitty_pty_write_byte(g_terminal_master_fd, byte_val);
}

static void nitty_terminal_poll_output(void)
{
    if (g_terminal_master_fd < 0 || g_terminal_shell_dead) return;

    /* Check if the shell has exited */
    if (g_terminal_child_pid > 0 && !nitty_pty_child_alive(g_terminal_child_pid)) {
        g_terminal_shell_dead = 1;
        /* Drain remaining output */
        int64_t n;
        do {
            n = nitty_pty_read_raw(g_terminal_master_fd,
                                    (int64_t)(uintptr_t)g_poll_buf, 16383);
            if (n > 0) {
                g_poll_buf[n] = '\0';
                nitty_grid_process_output(g_poll_buf, n);
                pty_queue_push(g_poll_buf, n);
            }
        } while (n > 0);

        /* Display exit message */
        const char *msg = "\r\n[Process exited]\r\n";
        nitty_grid_process_output(msg, (int64_t)strlen(msg));
        nitty_gtk4_queue_redraw();
        nitty_input_clear_terminal_mode();
        return;
    }

    /* Read and process output */
    int chunks = 0;
    int64_t n;
    do {
        n = nitty_pty_read_raw(g_terminal_master_fd,
                                (int64_t)(uintptr_t)g_poll_buf, 16383);
        if (n > 0) {
            g_poll_buf[n] = '\0';
            nitty_grid_process_output(g_poll_buf, n);
            pty_queue_push(g_poll_buf, n);
            chunks++;
        }
    } while (n > 0 && chunks < 8);  /* Max 8 chunks per tick (128KB) */

    if (chunks > 0) {
        nitty_gtk4_queue_redraw();
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

/* v0.4.0: Non-static so nitty_render.c can extern it for tab bar drawing */
cairo_t *g_draw_cr      = NULL;
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
        /* v0.3.0: Drive Nitpick pipeline sync before painting:
         *   tw_on_draw() drains g_pty_queue through pipeline_feed,
         *   then calls renderer_sync_frame() to push TerminalState → C render grid.
         *   nitty_render_frame() then paints the updated grid via Cairo/Pango. */
        tw_on_draw();
        /* v0.0.4 legacy: also drive C-side grid render (belt+suspenders) */
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

/* ═══════════════════════════════════════════════════════════════════════
 * v0.1.4: fd watching, idle callback, monotonic time
 * ═══════════════════════════════════════════════════════════════════════ */

#include <glib-unix.h>

static int g_fd_watch_ready = 0;
static guint g_fd_watch_id = 0;
static guint g_idle_id = 0;
static void (*g_idle_callback)(void) = NULL;

/* v0.3.2: Text blink timer state */
#include "nitty_render.h"
static guint g_blink_timer_id  = 0;
static int   g_blink_phase     = 1; /* 1=visible, 0=hidden */
static gboolean on_blink_tick(gpointer user_data); /* forward decl */

/* GLib fd watch callback — fires when PTY fd is readable */
static gboolean on_fd_readable(gint fd, GIOCondition condition, gpointer user_data)
{
    (void)fd;
    (void)user_data;
    if (condition & G_IO_IN) {
        g_fd_watch_ready = 1;
    }
    return G_SOURCE_CONTINUE;
}

int64_t nitty_gtk4_add_fd_watch(int64_t fd)
{
    if (fd < 0) return -1;
    g_fd_watch_id = g_unix_fd_add((gint)fd, G_IO_IN | G_IO_HUP | G_IO_ERR,
                                   on_fd_readable, NULL);
    if (g_fd_watch_id == 0) return -1;
    return 0;
}

int64_t nitty_gtk4_fd_watch_poll(void)
{
    return g_fd_watch_ready ? 1 : 0;
}

void nitty_gtk4_fd_watch_clear(void)
{
    g_fd_watch_ready = 0;
}

void nitty_gtk4_fd_watch_remove(void)
{
    if (g_fd_watch_id > 0) {
        g_source_remove(g_fd_watch_id);
        g_fd_watch_id = 0;
    }
    g_fd_watch_ready = 0;
}

int64_t nitty_gtk4_get_monotonic_time(void)
{
    return (int64_t)g_get_monotonic_time();
}

/* GLib idle callback — fires each main loop iteration */
static gboolean on_idle_tick(gpointer user_data)
{
    (void)user_data;
    if (g_idle_callback != NULL) {
        g_idle_callback();
    }
    /* v0.3.2: start blink timer if any blink cells are present */
    if (nitty_render_has_blink_cells() && g_blink_timer_id == 0) {
        g_blink_phase = 1;
        nitty_render_set_blink_visible(1);
        g_blink_timer_id = g_timeout_add(500, (GSourceFunc)on_blink_tick, NULL);
    }
    return G_SOURCE_CONTINUE;
}

void nitty_gtk4_set_idle_callback(void (*callback)(void))
{
    g_idle_callback = callback;
    if (g_idle_id == 0 && callback != NULL) {
        /* Use g_timeout_add instead of g_idle_add for a ~16ms tick (60fps) */
        g_idle_id = g_timeout_add(16, (GSourceFunc)on_idle_tick, NULL);
    }
}

void nitty_gtk4_queue_redraw(void)
{
    if (g_drawing_area != NULL) {
        gtk_widget_queue_draw(g_drawing_area);
    }
}

/* v0.3.3: Text blink timer callback — fires every 500ms */
static gboolean on_blink_tick(gpointer user_data)
{
    (void)user_data;
    g_blink_phase = !g_blink_phase;
    nitty_render_set_blink_visible(g_blink_phase);
    nitty_gtk4_queue_redraw();
    /* Stop timer if no more blink cells */
    if (!nitty_render_has_blink_cells()) {
        g_blink_timer_id = 0;
        nitty_render_set_blink_visible(1); /* restore visible for next time */
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

/* ═══════════════════════════════════════════════════════════════════════
 * v0.3.3: Focus controller
 * ═══════════════════════════════════════════════════════════════════════ */

static int g_terminal_focused = 1; /* assume focused at startup */

static void on_focus_in(GtkEventControllerFocus *ctrl, gpointer user_data)
{
    (void)ctrl;
    (void)user_data;
    g_terminal_focused = 1;
    /* Reset cursor blink to visible phase on focus gain */
    nitty_render_set_cursor_blink_phase(1);
    nitty_gtk4_queue_redraw();
}

static void on_focus_out(GtkEventControllerFocus *ctrl, gpointer user_data)
{
    (void)ctrl;
    (void)user_data;
    g_terminal_focused = 0;
    /* Show cursor as hollow outline while unfocused — force visible */
    nitty_render_set_cursor_blink_phase(1);
    nitty_gtk4_queue_redraw();
}

void nitty_gtk4_focus_enable(void)
{
    GtkWidget *target = (g_drawing_area != NULL) ? g_drawing_area : NULL;
    if (target == NULL) return;
    GtkEventController *focus_ctrl = gtk_event_controller_focus_new();
    g_signal_connect(focus_ctrl, "enter", G_CALLBACK(on_focus_in),  NULL);
    g_signal_connect(focus_ctrl, "leave", G_CALLBACK(on_focus_out), NULL);
    gtk_widget_add_controller(target, focus_ctrl);
}

int64_t nitty_gtk4_get_focused(void)
{
    return (int64_t)g_terminal_focused;
}

/* ═══════════════════════════════════════════════════════════════════════
 * v0.4.1: Multi-tab PTY lifecycle
 * ═══════════════════════════════════════════════════════════════════════ */

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

/* Forward declarations from nitty_pty_shim.c */
extern int64_t nitty_pty_openpt(void);
extern int64_t nitty_pty_grantpt(int64_t master_fd);
extern int64_t nitty_pty_unlockpt(int64_t master_fd);
extern int64_t nitty_pty_set_winsize(int64_t master_fd, int64_t rows, int64_t cols,
                                      int64_t xpixel, int64_t ypixel);
extern int64_t nitty_pty_set_nonblock(int64_t master_fd);
extern int64_t nitty_pty_spawn_shell(int64_t master_fd, int64_t rows, int64_t cols);
extern int64_t nitty_pty_close(int64_t master_fd);

/* Spawn a new PTY+shell for a tab.
 * Returns packed int64: (master_fd * 1000000 + pid), or -1 on failure.
 * Does NOT switch active PTY — call nitty_gtk4_set_active_pty_fd() after. */
int64_t nitty_gtk4_spawn_tab_shell(int64_t rows, int64_t cols)
{
    int64_t master_fd = nitty_pty_openpt();
    if (master_fd < 0) return -1;
    nitty_pty_grantpt(master_fd);
    nitty_pty_unlockpt(master_fd);
    nitty_pty_set_winsize(master_fd, rows, cols, 0, 0);
    nitty_pty_set_nonblock(master_fd);
    int64_t pid = nitty_pty_spawn_shell(master_fd, rows, cols);
    if (pid <= 0) {
        nitty_pty_close(master_fd);
        return -1;
    }
    return master_fd * 1000000LL + pid;
}

int64_t nitty_gtk4_spawn_result_fd(int64_t result)
{
    if (result < 0) return -1;
    return result / 1000000LL;
}

int64_t nitty_gtk4_spawn_result_pid(int64_t result)
{
    if (result < 0) return -1;
    return result % 1000000LL;
}

/* Switch the active PTY: idle poll and input writes use this fd/pid.
 * Clears the PTY byte queue so the new tab starts with a clean slate. */
void nitty_gtk4_set_active_pty_fd(int64_t master_fd, int64_t child_pid)
{
    g_terminal_master_fd  = master_fd;
    g_terminal_child_pid  = child_pid;
    g_terminal_shell_dead = 0;
    /* Clear the byte queue — discard leftover bytes from previous tab */
    g_pty_queue_write = 0;
    g_pty_queue_read  = 0;
    /* Update input routing so keys go to the new PTY fd */
    if (master_fd >= 0) {
        nitty_input_set_terminal_mode(master_fd);
    } else {
        nitty_input_clear_terminal_mode();
    }
}

/* Kill a tab's shell and close its PTY fd.
 * Sends SIGHUP; if still alive after ~100ms, sends SIGKILL. */
void nitty_gtk4_kill_tab_shell(int64_t master_fd, int64_t child_pid)
{
    if (child_pid > 0) {
        kill((pid_t)child_pid, SIGHUP);
        /* Brief spin-wait up to ~100ms for graceful exit */
        for (int i = 0; i < 10; i++) {
            int status;
            pid_t r = waitpid((pid_t)child_pid, &status, WNOHANG);
            if (r == child_pid) break;   /* exited */
            usleep(10000);               /* 10ms */
        }
        /* Force kill if still alive */
        kill((pid_t)child_pid, SIGKILL);
        waitpid((pid_t)child_pid, NULL, WNOHANG);
    }
    if (master_fd >= 0) {
        nitty_pty_close(master_fd);
    }
}

/* Callback state for synchronous close dialog */
typedef struct {
    GMainLoop *loop;
    int        result;  /* 1 = Close chosen, 0 = Cancel */
} ConfirmCloseState;

static void on_confirm_close_response(GObject      *source,
                                      GAsyncResult *result,
                                      gpointer      user_data)
{
    ConfirmCloseState *state = (ConfirmCloseState *)user_data;
    GtkAlertDialog    *dlg   = GTK_ALERT_DIALOG(source);
    GError            *err   = NULL;
    int button = gtk_alert_dialog_choose_finish(dlg, result, &err);
    if (err) {
        g_error_free(err);
        state->result = 0;
    } else {
        /* buttons array: [0]="Cancel", [1]="Close"
         * button == 1 means user chose "Close" */
        state->result = (button == 1) ? 1 : 0;
    }
    g_main_loop_quit(state->loop);
}

/* Show a synchronous close confirmation dialog.
 * Returns 1 = confirmed "Close", 0 = cancelled.
 * Uses GtkAlertDialog + nested GMainLoop (GTK4 >= 4.10). */
int64_t nitty_gtk4_confirm_close(const char *tab_title)
{
    char msg[512];
    snprintf(msg, sizeof(msg),
             "Terminal \"%s\" has a running process.",
             tab_title ? tab_title : "Shell");

    const char *buttons[] = { "Cancel", "Close", NULL };

    GtkAlertDialog *dlg = gtk_alert_dialog_new("%s", msg);
    gtk_alert_dialog_set_detail(dlg, "The terminal session will be terminated.");
    gtk_alert_dialog_set_buttons(dlg, buttons);
    gtk_alert_dialog_set_cancel_button(dlg, 0);   /* Cancel = index 0 */
    gtk_alert_dialog_set_default_button(dlg, 1);  /* Close  = index 1 */
    gtk_alert_dialog_set_modal(dlg, TRUE);

    ConfirmCloseState state;
    state.loop   = g_main_loop_new(NULL, FALSE);
    state.result = 0;

    gtk_alert_dialog_choose(dlg,
                            GTK_WINDOW(g_main_window),
                            NULL,
                            on_confirm_close_response,
                            &state);

    /* Block until the dialog is dismissed */
    g_main_loop_run(state.loop);
    g_main_loop_unref(state.loop);
    g_object_unref(dlg);

    return (int64_t)state.result;
}

/* ═══════════════════════════════════════════════════════════════════════
 * v0.4.3: Context menu (GtkPopoverMenu via GMenu)
 * ═══════════════════════════════════════════════════════════════════════ */

#include <gio/gio.h>  /* GMenu, GSimpleActionGroup */

static volatile int64_t g_ctx_pending_action = 0;
static volatile int64_t g_ctx_tab_idx = -1;

/* Action callback — maps action name → numeric code */
static void on_ctx_action(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)param;
    (void)user_data;
    const char *name = g_action_get_name(G_ACTION(action));
    if      (strcmp(name, "rename")         == 0) g_ctx_pending_action = 1;
    else if (strcmp(name, "duplicate")      == 0) g_ctx_pending_action = 2;
    else if (strcmp(name, "close")          == 0) g_ctx_pending_action = 3;
    else if (strcmp(name, "close-others")   == 0) g_ctx_pending_action = 4;
    else if (strcmp(name, "close-right")    == 0) g_ctx_pending_action = 5;
    else if (strcmp(name, "color-red")      == 0) g_ctx_pending_action = 6;
    else if (strcmp(name, "color-orange")   == 0) g_ctx_pending_action = 7;
    else if (strcmp(name, "color-yellow")   == 0) g_ctx_pending_action = 8;
    else if (strcmp(name, "color-green")    == 0) g_ctx_pending_action = 9;
    else if (strcmp(name, "color-blue")     == 0) g_ctx_pending_action = 10;
    else if (strcmp(name, "color-purple")   == 0) g_ctx_pending_action = 11;
    else if (strcmp(name, "color-pink")     == 0) g_ctx_pending_action = 12;
    else if (strcmp(name, "color-none")     == 0) g_ctx_pending_action = 13;
    else if (strcmp(name, "explode-panes")  == 0) g_ctx_pending_action = 14; /* v0.5.5 */
}

void nitty_gtk4_context_menu_show(int64_t x, int64_t y, int64_t tab_idx)
{
    if (g_drawing_area == NULL) return;

    g_ctx_tab_idx = tab_idx;

    /* Build action group */
    GSimpleActionGroup *ag = g_simple_action_group_new();
    const char *actions[] = {
        "rename", "duplicate", "close", "close-others", "close-right",
        "color-red", "color-orange", "color-yellow", "color-green",
        "color-blue", "color-purple", "color-pink", "color-none",
        "explode-panes",   /* v0.5.5 */
        NULL
    };
    for (int i = 0; actions[i]; i++) {
        GSimpleAction *a = g_simple_action_new(actions[i], NULL);
        g_signal_connect(a, "activate", G_CALLBACK(on_ctx_action), NULL);
        g_action_map_add_action(G_ACTION_MAP(ag), G_ACTION(a));
        g_object_unref(a);
    }
    gtk_widget_insert_action_group(g_drawing_area, "tab", G_ACTION_GROUP(ag));
    g_object_unref(ag);

    /* Build GMenu */
    GMenu *menu = g_menu_new();
    g_menu_append(menu, "Rename Tab",             "tab.rename");
    g_menu_append(menu, "Duplicate Tab",          "tab.duplicate");

    GMenu *close_section = g_menu_new();
    g_menu_append(close_section, "Close Tab",              "tab.close");
    g_menu_append(close_section, "Close Other Tabs",       "tab.close-others");
    g_menu_append(close_section, "Close Tabs to the Right","tab.close-right");
    g_menu_append_section(menu, NULL, G_MENU_MODEL(close_section));
    g_object_unref(close_section);

    GMenu *color_menu = g_menu_new();
    g_menu_append(color_menu, "Red",    "tab.color-red");
    g_menu_append(color_menu, "Orange", "tab.color-orange");
    g_menu_append(color_menu, "Yellow", "tab.color-yellow");
    g_menu_append(color_menu, "Green",  "tab.color-green");
    g_menu_append(color_menu, "Blue",   "tab.color-blue");
    g_menu_append(color_menu, "Purple", "tab.color-purple");
    g_menu_append(color_menu, "Pink",   "tab.color-pink");
    g_menu_append(color_menu, "None",   "tab.color-none");
    GMenuItem *color_item = g_menu_item_new_submenu("Set Tab Color",
                                                     G_MENU_MODEL(color_menu));
    g_menu_append_item(menu, color_item);
    g_object_unref(color_item);
    g_object_unref(color_menu);

    /* v0.5.5: Pane operations section */
    GMenu *pane_section = g_menu_new();
    g_menu_append(pane_section, "Explode Panes to Tabs", "tab.explode-panes");
    g_menu_append_section(menu, NULL, G_MENU_MODEL(pane_section));
    g_object_unref(pane_section);

    /* Create and show popover */
    GtkWidget *popover = gtk_popover_menu_new_from_model(G_MENU_MODEL(menu));
    g_object_unref(menu);

    gtk_widget_set_parent(popover, g_drawing_area);
    GdkRectangle rect = { (int)x, (int)y, 1, 1 };
    gtk_popover_set_pointing_to(GTK_POPOVER(popover), &rect);
    gtk_popover_set_has_arrow(GTK_POPOVER(popover), FALSE);
    gtk_popover_popup(GTK_POPOVER(popover));
}

int64_t nitty_gtk4_context_menu_poll(void)
{
    int64_t action = g_ctx_pending_action;
    g_ctx_pending_action = 0;
    return action;
}

int64_t nitty_gtk4_context_menu_get_tab_idx(void)
{
    return g_ctx_tab_idx;
}

/* ═══════════════════════════════════════════════════════════════════════
 * v0.4.3: Rename dialog (synchronous modal GtkWindow + GtkEntry)
 * ═══════════════════════════════════════════════════════════════════════ */

typedef struct {
    GMainLoop *loop;
    char       result[512];
    int        confirmed;
} PromptState;

static void on_prompt_ok(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    PromptState *state = (PromptState *)user_data;
    state->confirmed = 1;
    g_main_loop_quit(state->loop);
}

static void on_prompt_cancel(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    PromptState *state = (PromptState *)user_data;
    state->confirmed = 0;
    g_main_loop_quit(state->loop);
}

static void on_prompt_entry_activate(GtkEntry *entry, gpointer user_data)
{
    (void)entry;
    PromptState *state = (PromptState *)user_data;
    state->confirmed = 1;
    g_main_loop_quit(state->loop);
}

static char g_prompt_result[512];

const char *nitty_gtk4_prompt_string(const char *prompt, const char *current_value)
{
    g_prompt_result[0] = '\0';

    PromptState state;
    state.loop      = g_main_loop_new(NULL, FALSE);
    state.confirmed = 0;
    state.result[0] = '\0';

    /* Dialog window */
    GtkWidget *dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), prompt ? prompt : "Rename Tab");
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(g_main_window));
    gtk_window_set_default_size(GTK_WINDOW(dialog), 360, -1);
    gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);

    /* Layout */
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(box, 16);
    gtk_widget_set_margin_end(box, 16);
    gtk_widget_set_margin_top(box, 16);
    gtk_widget_set_margin_bottom(box, 16);
    gtk_window_set_child(GTK_WINDOW(dialog), box);

    GtkWidget *label = gtk_label_new(prompt ? prompt : "Enter new tab name:");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), label);

    GtkWidget *entry = gtk_entry_new();
    if (current_value && current_value[0]) {
        gtk_editable_set_text(GTK_EDITABLE(entry), current_value);
        gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);
    }
    gtk_box_append(GTK_BOX(box), entry);
    g_signal_connect(entry, "activate", G_CALLBACK(on_prompt_entry_activate), &state);

    GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(btn_box, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(box), btn_box);

    GtkWidget *cancel_btn = gtk_button_new_with_label("Cancel");
    GtkWidget *ok_btn     = gtk_button_new_with_label("Rename");
    gtk_box_append(GTK_BOX(btn_box), cancel_btn);
    gtk_box_append(GTK_BOX(btn_box), ok_btn);

    g_signal_connect(cancel_btn, "clicked", G_CALLBACK(on_prompt_cancel), &state);
    g_signal_connect(ok_btn,     "clicked", G_CALLBACK(on_prompt_ok),     &state);

    gtk_widget_set_visible(dialog, TRUE);
    g_main_loop_run(state.loop);

    if (state.confirmed) {
        const char *text = gtk_editable_get_text(GTK_EDITABLE(entry));
        if (text && text[0]) {
            strncpy(g_prompt_result, text, sizeof(g_prompt_result) - 1);
            g_prompt_result[sizeof(g_prompt_result) - 1] = '\0';
        }
    }

    gtk_window_destroy(GTK_WINDOW(dialog));
    g_main_loop_unref(state.loop);

    return g_prompt_result;
}

/* ═══════════════════════════════════════════════════════════════════════
 * v0.4.3: Spawn tab shell at CWD (/proc/pid/cwd)
 * ═══════════════════════════════════════════════════════════════════════ */

extern int64_t nitty_pty_spawn_shell_at(int64_t master_fd, int64_t rows, int64_t cols,
                                         const char *cwd);

int64_t nitty_gtk4_spawn_tab_shell_at_cwd(int64_t rows, int64_t cols, int64_t source_pid)
{
    /* Resolve /proc/<pid>/cwd via readlink */
    char cwd[4096] = {0};
    if (source_pid > 0) {
        char proc_path[64];
        snprintf(proc_path, sizeof(proc_path), "/proc/%lld/cwd", (long long)source_pid);
        ssize_t len = readlink(proc_path, cwd, sizeof(cwd) - 1);
        if (len <= 0) cwd[0] = '\0';
    }

    int64_t master_fd = nitty_pty_openpt();
    if (master_fd < 0) return -1;
    nitty_pty_grantpt(master_fd);
    nitty_pty_unlockpt(master_fd);
    nitty_pty_set_winsize(master_fd, rows, cols, 0, 0);
    nitty_pty_set_nonblock(master_fd);

    /* Prefer CWD spawn; fall back to plain spawn if not available */
    int64_t pid = -1;
    if (cwd[0] != '\0') {
        /* Try the extended spawn; fall back if the symbol isn't linked */
        pid = nitty_pty_spawn_shell_at(master_fd, rows, cols, cwd);
    }
    if (pid <= 0) {
        pid = nitty_pty_spawn_shell(master_fd, rows, cols);
    }
    if (pid <= 0) {
        nitty_pty_close(master_fd);
        return -1;
    }
    return master_fd * 1000000LL + pid;
}

/* ═══════════════════════════════════════════════════════════════════════
 * v0.4.3: Completion event poll (waitpid WNOHANG on registered PIDs)
 * ═══════════════════════════════════════════════════════════════════════ */

static int64_t g_tab_pids[16]  = { -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1 };
static int64_t g_completion_pending_slot = -1;

void nitty_gtk4_register_tab_pid(int64_t slot, int64_t pid)
{
    if (slot < 0 || slot >= 16) return;
    g_tab_pids[slot] = pid;
}

void nitty_gtk4_unregister_tab_pid(int64_t slot)
{
    if (slot < 0 || slot >= 16) return;
    g_tab_pids[slot] = -1;
}

/* Returns the slot index of a tab whose shell exited, or -1 if none.
 * Skips the active tab (index that matches g_terminal_child_pid). */
int64_t nitty_gtk4_completion_event_poll(void)
{
    if (g_completion_pending_slot >= 0) {
        int64_t s = g_completion_pending_slot;
        g_completion_pending_slot = -1;
        return s;
    }
    for (int i = 0; i < 16; i++) {
        int64_t pid = g_tab_pids[i];
        if (pid <= 0) continue;
        /* Skip the currently active tab */
        if (pid == g_terminal_child_pid) continue;
        int status;
        pid_t r = waitpid((pid_t)pid, &status, WNOHANG);
        if (r == (pid_t)pid) {
            /* Process exited */
            g_tab_pids[i] = -1;
            return (int64_t)i;
        }
    }
    return -1;
}

/* ═══════════════════════════════════════════════════════════════════════
 * v0.4.4: Session persistence helpers
 * ═══════════════════════════════════════════════════════════════════════ */

/* Read the CWD of a process from /proc/<pid>/cwd via readlink.
 * Returns a NUL-terminated path string, or "" on any error.
 * Uses a static buffer — safe on the single GTK main thread. */
const char *nitty_gtk4_get_proc_cwd(int64_t pid)
{
    static char s_cwd_buf[4096];
    s_cwd_buf[0] = '\0';

    if (pid <= 0) return s_cwd_buf;

    char link_path[64];
    snprintf(link_path, sizeof(link_path), "/proc/%lld/cwd", (long long)pid);

    ssize_t n = readlink(link_path, s_cwd_buf, sizeof(s_cwd_buf) - 1);
    if (n > 0) {
        s_cwd_buf[n] = '\0';
    } else {
        s_cwd_buf[0] = '\0';
    }
    return s_cwd_buf;
}

const char *nitty_gtk4_get_proc_comm(int64_t pid)
{
    static char s_comm_buf[32];
    s_comm_buf[0] = '\0';

    if (pid <= 0) return s_comm_buf;

    char path[64];
    snprintf(path, sizeof(path), "/proc/%lld/comm", (long long)pid);

    FILE *f = fopen(path, "r");
    if (!f) return s_comm_buf;

    size_t n = fread(s_comm_buf, 1, sizeof(s_comm_buf) - 1, f);
    fclose(f);
    s_comm_buf[n] = '\0';
    /* Strip trailing newline */
    while (n > 0 && (s_comm_buf[n-1] == '\n' || s_comm_buf[n-1] == '\r')) {
        s_comm_buf[--n] = '\0';
    }
    return s_comm_buf;
}

/* Spawn PTY + shell at an explicit `path` directory.
 * Falls back to $HOME if path is NULL, empty, or non-existent.
 * Returns (master_fd * 1000000 + child_pid), or -1 on failure. */
int64_t nitty_gtk4_spawn_tab_shell_at_path(int64_t rows, int64_t cols,
                                           const char *path)
{
    extern int64_t nitty_pty_openpt(void);
    extern int64_t nitty_pty_spawn_shell(int64_t master_fd, int64_t rows, int64_t cols);
    extern int64_t nitty_pty_close(int64_t fd);
    extern int64_t nitty_pty_spawn_shell_at(int64_t master_fd, int64_t rows,
                                            int64_t cols, const char *cwd);

    int master_fd = (int)nitty_pty_openpt();
    if (master_fd < 0) return -1;

    /* Resolve path: use supplied path if valid dir, else fall back to $HOME */
    char resolved[4096];
    resolved[0] = '\0';

    if (path && path[0] != '\0') {
        struct stat st;
        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
            snprintf(resolved, sizeof(resolved), "%s", path);
        }
    }

    /* Fall back to $HOME if resolved is empty */
    if (resolved[0] == '\0') {
        const char *home = getenv("HOME");
        if (home) snprintf(resolved, sizeof(resolved), "%s", home);
    }

    int64_t pid = nitty_pty_spawn_shell_at((int64_t)master_fd, rows, cols, resolved);
    if (pid <= 0) {
        pid = nitty_pty_spawn_shell((int64_t)master_fd, rows, cols);
    }
    if (pid <= 0) {
        nitty_pty_close((int64_t)master_fd);
        return -1;
    }
    return (int64_t)master_fd * 1000000LL + pid;
}

/* v0.4.4: Return CLOCK_MONOTONIC seconds as int64_t. */
#include <time.h>
int64_t nitty_gtk4_monotonic_sec(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (int64_t)ts.tv_sec;
}


/* ═══════════════════════════════════════════════════════════════════════════
 * v0.5.1: Split pane support — GtkPaned, GtkBox, content widget management
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Current content widget set via nitty_gtk4_set_content_widget */
static GtkWidget *g_content_widget = NULL;

int64_t nitty_gtk4_paned_new(int64_t orientation)
{
    GtkOrientation o = (orientation == 1)
        ? GTK_ORIENTATION_VERTICAL
        : GTK_ORIENTATION_HORIZONTAL;
    GtkWidget *paned = gtk_paned_new(o);
    if (paned == NULL) return 0;
    gtk_widget_set_hexpand(paned, TRUE);
    gtk_widget_set_vexpand(paned, TRUE);
    return (int64_t)(uintptr_t)paned;
}

void nitty_gtk4_paned_set_start_child(int64_t paned_ptr, int64_t child_ptr)
{
    if (paned_ptr == 0 || child_ptr == 0) return;
    GtkWidget *paned = (GtkWidget *)(uintptr_t)paned_ptr;
    GtkWidget *child = (GtkWidget *)(uintptr_t)child_ptr;
    gtk_paned_set_start_child(GTK_PANED(paned), child);
    gtk_paned_set_resize_start_child(GTK_PANED(paned), TRUE);
    gtk_paned_set_shrink_start_child(GTK_PANED(paned), FALSE);
}

void nitty_gtk4_paned_set_end_child(int64_t paned_ptr, int64_t child_ptr)
{
    if (paned_ptr == 0 || child_ptr == 0) return;
    GtkWidget *paned = (GtkWidget *)(uintptr_t)paned_ptr;
    GtkWidget *child = (GtkWidget *)(uintptr_t)child_ptr;
    gtk_paned_set_end_child(GTK_PANED(paned), child);
    gtk_paned_set_resize_end_child(GTK_PANED(paned), TRUE);
    gtk_paned_set_shrink_end_child(GTK_PANED(paned), FALSE);
}

void nitty_gtk4_paned_set_position(int64_t paned_ptr, int64_t position)
{
    if (paned_ptr == 0) return;
    GtkWidget *paned = (GtkWidget *)(uintptr_t)paned_ptr;
    gtk_paned_set_position(GTK_PANED(paned), (int)position);
}

int64_t nitty_gtk4_paned_get_position(int64_t paned_ptr)
{
    if (paned_ptr == 0) return 0;
    GtkWidget *paned = (GtkWidget *)(uintptr_t)paned_ptr;
    return (int64_t)gtk_paned_get_position(GTK_PANED(paned));
}

int64_t nitty_gtk4_pane_drawing_area_new(void)
{
    GtkWidget *da = gtk_drawing_area_new();
    if (da == NULL) return 0;
    gtk_widget_set_hexpand(da, TRUE);
    gtk_widget_set_vexpand(da, TRUE);
    return (int64_t)(uintptr_t)da;
}

void nitty_gtk4_set_content_widget(int64_t widget_ptr)
{
    if (g_main_window == NULL) return;
    GtkWidget *widget = (widget_ptr != 0)
        ? (GtkWidget *)(uintptr_t)widget_ptr
        : NULL;
    gtk_window_set_child(GTK_WINDOW(g_main_window), widget);
    g_content_widget = widget;
}

int64_t nitty_gtk4_get_content_widget(void)
{
    return (int64_t)(uintptr_t)g_content_widget;
}

int64_t nitty_gtk4_box_new(int64_t orientation, int64_t spacing)
{
    GtkOrientation o = (orientation == 1)
        ? GTK_ORIENTATION_VERTICAL
        : GTK_ORIENTATION_HORIZONTAL;
    GtkWidget *box = gtk_box_new(o, (int)spacing);
    if (box == NULL) return 0;
    gtk_widget_set_hexpand(box, TRUE);
    gtk_widget_set_vexpand(box, TRUE);
    return (int64_t)(uintptr_t)box;
}

void nitty_gtk4_box_append(int64_t box_ptr, int64_t child_ptr)
{
    if (box_ptr == 0 || child_ptr == 0) return;
    GtkWidget *box   = (GtkWidget *)(uintptr_t)box_ptr;
    GtkWidget *child = (GtkWidget *)(uintptr_t)child_ptr;
    gtk_box_append(GTK_BOX(box), child);
}

void nitty_gtk4_widget_set_expand(int64_t widget_ptr, int64_t fill_h, int64_t fill_v)
{
    if (widget_ptr == 0) return;
    GtkWidget *w = (GtkWidget *)(uintptr_t)widget_ptr;
    gtk_widget_set_hexpand(w, fill_h != 0);
    gtk_widget_set_vexpand(w, fill_v != 0);
}

/* ═══════════════════════════════════════════════════════════════════════
 * v0.5.3: Notification toast
 * ═══════════════════════════════════════════════════════════════════════ */

/* Timeout callback: hides and destroys the notification window */
static gboolean _notify_dismiss_cb(gpointer data)
{
    GtkWidget *win = (GtkWidget *)data;
    if (win != NULL) {
        gtk_window_destroy(GTK_WINDOW(win));
    }
    return G_SOURCE_REMOVE;
}

void nitty_gtk4_show_notification(const char *msg)
{
    if (msg == NULL) return;

    /* Create a transient child window styled as a toast */
    GtkWidget *parent = g_main_window;
    GtkWidget *popup  = gtk_window_new();
    if (popup == NULL) return;

    gtk_window_set_decorated(GTK_WINDOW(popup), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(popup), FALSE);
    gtk_window_set_default_size(GTK_WINDOW(popup), 320, 40);
    if (parent != NULL) {
        gtk_window_set_transient_for(GTK_WINDOW(popup), GTK_WINDOW(parent));
        gtk_window_set_modal(GTK_WINDOW(popup), FALSE);
    }

    GtkWidget *label = gtk_label_new(msg);
    gtk_widget_set_margin_start(label, 12);
    gtk_widget_set_margin_end(label, 12);
    gtk_widget_set_margin_top(label, 8);
    gtk_widget_set_margin_bottom(label, 8);
    gtk_window_set_child(GTK_WINDOW(popup), label);

    gtk_widget_set_visible(popup, TRUE);

    /* Auto-dismiss after 2000ms */
    g_timeout_add(2000, _notify_dismiss_cb, popup);
}

/* ═══════════════════════════════════════════════════════════════════════
 * v0.5.4: Broadcast input — delegate to nitty_input.c internals
 * ═══════════════════════════════════════════════════════════════════════ */

/* Externals declared in nitty_input.c */
extern void nitty_input_broadcast_begin(void);
extern void nitty_input_broadcast_add_fd(int64_t fd);
extern void nitty_input_broadcast_clear(void);

void nitty_gtk4_broadcast_begin(void)
{
    nitty_input_broadcast_begin();
}

void nitty_gtk4_broadcast_add_fd(int64_t fd)
{
    nitty_input_broadcast_add_fd(fd);
}

void nitty_gtk4_broadcast_clear(void)
{
    nitty_input_broadcast_clear();
}

int64_t nitty_gtk4_broadcast_is_active(void)
{
    /* A broadcast count > 0 means broadcast is active.
     * We check indirectly: try adding a sentinel (-1) then clear; easier
     * to just expose a small flag from nitty_input.c. */
    /* Simple: delegate to a helper we'll define inline here */
    extern int nitty_input_broadcast_count(void);
    return (nitty_input_broadcast_count() > 0) ? 1 : 0;
}

/* v0.5.5: Swap mode — delegates to nitty_input.c */
extern void nitty_input_set_swap_mode(int active);

void nitty_gtk4_set_swap_mode(int64_t active)
{
    nitty_input_set_swap_mode((int)active);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * v0.6.1: Profile-aware tab spawn: explicit shell binary + working directory.
 * Returns (master_fd * 1000000 + pid), same as nitty_gtk4_spawn_tab_shell.
 * shell_bin: empty string or NULL → fall back to $SHELL.
 * cwd:       empty string or NULL → fall back to $HOME.
 * ─────────────────────────────────────────────────────────────────────────────*/
int64_t nitty_gtk4_spawn_tab_shell_cmd(int64_t rows, int64_t cols,
                                        const char *cwd, const char *shell_bin)
{
    extern int64_t nitty_pty_openpt(void);
    extern int64_t nitty_pty_spawn_shell_cmd(int64_t master_fd, int64_t rows,
                                              int64_t cols, const char *cwd,
                                              const char *shell_bin);
    extern int64_t nitty_pty_spawn_shell_at(int64_t master_fd, int64_t rows,
                                             int64_t cols, const char *cwd);
    extern int64_t nitty_pty_spawn_shell(int64_t master_fd, int64_t rows,
                                          int64_t cols);
    extern int64_t nitty_pty_close(int64_t fd);

    int master_fd = (int)nitty_pty_openpt();
    if (master_fd < 0) return -1;

    /* Validate cwd — use $HOME as fallback if path is not a directory */
    char resolved_cwd[4096];
    resolved_cwd[0] = '\0';
    if (cwd && cwd[0] != '\0') {
        struct stat st;
        if (stat(cwd, &st) == 0 && S_ISDIR(st.st_mode)) {
            snprintf(resolved_cwd, sizeof(resolved_cwd), "%s", cwd);
        }
    }
    if (resolved_cwd[0] == '\0') {
        const char *home = getenv("HOME");
        if (home) snprintf(resolved_cwd, sizeof(resolved_cwd), "%s", home);
    }

    int64_t pid = -1;

    /* Try profile-specified shell+cwd first */
    if (shell_bin && shell_bin[0] != '\0') {
        pid = nitty_pty_spawn_shell_cmd((int64_t)master_fd, rows, cols,
                                         resolved_cwd, shell_bin);
    }

    /* Fall back to CWD-only spawn (uses $SHELL) */
    if (pid <= 0) {
        pid = nitty_pty_spawn_shell_at((int64_t)master_fd, rows, cols,
                                        resolved_cwd);
    }

    /* Last resort: plain spawn */
    if (pid <= 0) {
        pid = nitty_pty_spawn_shell((int64_t)master_fd, rows, cols);
    }

    if (pid <= 0) {
        nitty_pty_close((int64_t)master_fd);
        return -1;
    }

    return (int64_t)master_fd * 1000000LL + pid;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * v0.6.2: Hotkey engine support
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * Return CLOCK_MONOTONIC time in milliseconds.
 * Used by the Nitpick hotkey engine for multi-chord timeout tracking.
 */
int64_t nitty_gtk4_get_monotonic_ms(void)
{
    return (int64_t)(g_get_monotonic_time() / 1000LL);
}

/**
 * Return 1 if the last key event was intercepted (consumed) by the C shim
 * (i.e. it set a tab_event, pane_event, or scroll_event and returned TRUE).
 * Return 0 if the key was not consumed.
 *
 * The Nitpick hotkey engine should query this before dispatching, to avoid
 * double-handling keys already processed by the C interceptor.
 *
 * Implementation: we track consumption via g_last_key_consumed, which is
 * set whenever on_key_pressed returns TRUE for an intercepted key.
 */
static int g_last_key_consumed = 0;

int64_t nitty_gtk4_key_was_consumed(void)
{
    int consumed = g_last_key_consumed;
    g_last_key_consumed = 0;  /* clear after read */
    return (int64_t)consumed;
}

/* Internal helper: mark the last key as consumed (called from nitty_input.c
 * on_key_pressed via the shared consumed flag). Since both files are compiled
 * into the same shim .so, we can use a weak symbol approach or just expose
 * a setter. We use a setter called from nitty_input.c's callback. */
void nitty_gtk4_set_key_consumed(int consumed)
{
    g_last_key_consumed = consumed;
}

/**
 * v0.7.1 Clipboard — Copy & Paste
 *
 * Copy: nitty_gtk4_clipboard_copy_text(text)
 *   Writes text to the system clipboard via GdkClipboard.
 *
 * Paste (async):
 *   1. Nitpick calls nitty_gtk4_clipboard_paste_request() to start read.
 *   2. GDK calls on_paste_text() asynchronously.
 *   3. Nitpick polls nitty_gtk4_clipboard_paste_ready() each frame.
 *   4. When ready==1, Nitpick reads bytes via nitty_gtk4_clipboard_paste_get_byte(i).
 *
 * Primary selection (X11 middle-click):
 *   nitty_gtk4_primary_paste_request() — same flow, uses PRIMARY clipboard.
 *
 * The legacy stub functions (copy/paste with no args) remain as no-ops
 * for backward compatibility with existing hotkey dispatch.
 */

/* Selection text buffer (written by Nitpick via copy_text) */
static char g_selection_text[1024 * 1024];

/* Paste text buffer (written by async callback) */
static char g_paste_text[1024 * 1024];
static int  g_paste_ready = 0;

/* Legacy stubs — kept for backward compat with edit.copy/edit.paste dispatch.
 * The real work is now done by nitty_gtk4_clipboard_copy_text / paste_request. */
void nitty_gtk4_clipboard_copy(void)  { /* superseded by copy_text */ }
void nitty_gtk4_clipboard_paste(void) { /* superseded by paste_request */ }

/* Copy selection text to the system clipboard. */
void nitty_gtk4_clipboard_copy_text(const char *text)
{
    if (!text) return;
    GdkDisplay  *display = gdk_display_get_default();
    if (!display) return;
    GdkClipboard *cb = gdk_display_get_clipboard(display);
    if (!cb) return;
    gdk_clipboard_set_text(cb, text);
    /* Keep a local copy for primary selection auto-set */
    strncpy(g_selection_text, text, sizeof(g_selection_text) - 1);
    g_selection_text[sizeof(g_selection_text) - 1] = '\0';
    /* Also set the primary (X11 middle-click) selection on copy */
#ifdef GDK_WINDOWING_X11
    GdkClipboard *primary = gdk_display_get_primary_clipboard(display);
    if (primary) gdk_clipboard_set_text(primary, text);
#endif
}

/* Async callback — fired when clipboard read completes */
static void on_paste_text_ready(GObject *source, GAsyncResult *res,
                                gpointer user_data)
{
    (void)user_data;
    GError *err = NULL;
    char *text = gdk_clipboard_read_text_finish(GDK_CLIPBOARD(source), res, &err);
    if (text) {
        strncpy(g_paste_text, text, sizeof(g_paste_text) - 1);
        g_paste_text[sizeof(g_paste_text) - 1] = '\0';
        g_free(text);
    } else {
        g_paste_text[0] = '\0';
        if (err) g_error_free(err);
    }
    g_paste_ready = 1;
}

/* Start an async clipboard read. Nitpick polls paste_ready() each frame. */
void nitty_gtk4_clipboard_paste_request(void)
{
    g_paste_ready  = 0;
    g_paste_text[0] = '\0';
    GdkDisplay  *display = gdk_display_get_default();
    if (!display) { g_paste_ready = 1; return; }
    GdkClipboard *cb = gdk_display_get_clipboard(display);
    if (!cb) { g_paste_ready = 1; return; }
    gdk_clipboard_read_text_async(cb, NULL, on_paste_text_ready, NULL);
}

/* Start an async PRIMARY selection read (X11 middle-click). */
void nitty_gtk4_primary_paste_request(void)
{
    g_paste_ready  = 0;
    g_paste_text[0] = '\0';
    GdkDisplay  *display = gdk_display_get_default();
    if (!display) { g_paste_ready = 1; return; }
#ifdef GDK_WINDOWING_X11
    GdkClipboard *cb = gdk_display_get_primary_clipboard(display);
#else
    GdkClipboard *cb = gdk_display_get_clipboard(display);
#endif
    if (!cb) { g_paste_ready = 1; return; }
    gdk_clipboard_read_text_async(cb, NULL, on_paste_text_ready, NULL);
}

/* Poll: returns 1 when paste text is ready to read. */
int64_t nitty_gtk4_clipboard_paste_ready(void)
{
    return (int64_t)g_paste_ready;
}

/* Length of the pending paste text (bytes). */
int64_t nitty_gtk4_clipboard_paste_text_len(void)
{
    return (int64_t)strlen(g_paste_text);
}

/* Read one byte from the paste text buffer at position offset. */
int64_t nitty_gtk4_clipboard_paste_get_byte(int64_t offset)
{
    size_t len = strlen(g_paste_text);
    if (offset < 0 || (size_t)offset >= len) return 0;
    return (int64_t)(unsigned char)g_paste_text[(size_t)offset];
}


/* ═══════════════════════════════════════════════════════════════════════
 * v0.6.3: Global Hotkey Registration + Quake Mode Window Management
 * ═══════════════════════════════════════════════════════════════════════
 *
 * GTK4 notes:
 *   - GDK event filters (GdkFilterReturn, gdk_window_add_filter) were removed
 *     in GTK4. Instead we use XCheckMaskEvent() polling inside the idle-driven
 *     nitty_global_hotkey_poll() to drain grabbed KeyPress events from the X11
 *     event queue every frame.
 *   - gtk_window_set_keep_above / set_skip_taskbar_hint were removed from GTK4's
 *     GtkWindow API. The equivalent is now on the GdkSurface via the X11 backend:
 *     gdk_x11_surface_set_skip_taskbar_hint(), etc.
 * ═══════════════════════════════════════════════════════════════════════ */

#ifdef GDK_WINDOWING_X11
#include <gdk/x11/gdkx.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#endif

/* Global hotkey state (X11) */
static volatile int g_global_hotkey_fired = 0;
static int          g_global_hotkey_keycode  = 0;
static unsigned int g_global_hotkey_modmask  = 0;

#ifdef GDK_WINDOWING_X11

/* Convert Nitpick modifier bitmask → X11 modifier mask.
 * Nitpick/hotkey.npk: Shift=1, Ctrl=2, Alt=4, Super=8
 * X11: ShiftMask=1, ControlMask=4, Mod1Mask=8, Mod4Mask=64              */
static unsigned int _gdk_mods_to_x11(int64_t npk_mods)
{
    unsigned int m = 0;
    if (npk_mods & 1) m |= ShiftMask;    /* Shift  */
    if (npk_mods & 2) m |= ControlMask;  /* Ctrl   */
    if (npk_mods & 4) m |= Mod1Mask;     /* Alt    */
    if (npk_mods & 8) m |= Mod4Mask;     /* Super  */
    return m;
}

#endif /* GDK_WINDOWING_X11 */

/**
 * Returns 1 if the GDK display is an X11 display, 0 for Wayland or other.
 */
int64_t nitty_global_hotkey_is_x11(void)
{
#ifdef GDK_WINDOWING_X11
    GdkDisplay *dpy = gdk_display_get_default();
    if (dpy && GDK_IS_X11_DISPLAY(dpy))
        return 1;
#endif
    return 0;
}

/**
 * Register a global hotkey via XGrabKey (X11 only).
 * keyval:   GDK keyval integer (e.g. nitty_gdk_key_f12() = 65481).
 * mod_mask: Nitpick modifier bitmask (Shift=1, Ctrl=2, Alt=4, Super=8).
 *           Pass 0 for no modifiers (bare F12 etc.).
 * Returns 1 on success, 0 if not X11 or grab failed.
 *
 * Grab variants: we grab with and without NumLock (Mod2) and CapsLock
 * so the hotkey fires regardless of those lock states.
 */
int64_t nitty_x11_grab_key(int64_t keyval, int64_t mod_mask)
{
#ifdef GDK_WINDOWING_X11
    GdkDisplay *gdk_dpy = gdk_display_get_default();
    if (!gdk_dpy || !GDK_IS_X11_DISPLAY(gdk_dpy))
        return 0;

    Display *xdpy = gdk_x11_display_get_xdisplay(gdk_dpy);
    if (!xdpy) return 0;

    /* Map GDK keyval → X11 KeySym → keycode */
    KeySym   ksym = (KeySym)keyval;
    KeyCode  kc   = XKeysymToKeycode(xdpy, ksym);
    if (kc == 0) {
        g_printerr("nitty_x11_grab_key: unknown keyval %ld\n", (long)keyval);
        return 0;
    }

    unsigned int xmods = _gdk_mods_to_x11(mod_mask);
    Window root = DefaultRootWindow(xdpy);

    /* Grab with all Lock-key combinations */
    XGrabKey(xdpy, kc, xmods,                         root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(xdpy, kc, xmods | Mod2Mask,              root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(xdpy, kc, xmods | LockMask,              root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(xdpy, kc, xmods | Mod2Mask | LockMask,   root, True, GrabModeAsync, GrabModeAsync);
    XSync(xdpy, False);

    g_global_hotkey_keycode = (int)kc;
    g_global_hotkey_modmask = xmods;
    g_global_hotkey_fired   = 0;

    /* Select KeyPress events on the root window so XCheckMaskEvent can find them.
     * Note: XSelectInput may conflict with other apps doing the same — this is
     * unavoidable without a proper compositor protocol. */
    XSelectInput(xdpy, root, KeyPressMask);

    return 1;
#else
    (void)keyval; (void)mod_mask;
    return 0;
#endif
}

/**
 * Unregister the global hotkey.
 */
void nitty_x11_ungrab_key(int64_t keyval, int64_t mod_mask)
{
#ifdef GDK_WINDOWING_X11
    GdkDisplay *gdk_dpy = gdk_display_get_default();
    if (!gdk_dpy || !GDK_IS_X11_DISPLAY(gdk_dpy)) return;
    Display *xdpy = gdk_x11_display_get_xdisplay(gdk_dpy);
    if (!xdpy) return;

    KeySym   ksym = (KeySym)keyval;
    KeyCode  kc   = XKeysymToKeycode(xdpy, ksym);
    if (kc == 0) return;

    unsigned int xmods = _gdk_mods_to_x11(mod_mask);
    Window root = DefaultRootWindow(xdpy);
    XUngrabKey(xdpy, kc, xmods,                       root);
    XUngrabKey(xdpy, kc, xmods | Mod2Mask,            root);
    XUngrabKey(xdpy, kc, xmods | LockMask,            root);
    XUngrabKey(xdpy, kc, xmods | Mod2Mask | LockMask, root);
    XSync(xdpy, False);

    g_global_hotkey_keycode = 0;
    g_global_hotkey_modmask = 0;
    g_global_hotkey_fired   = 0;
#else
    (void)keyval; (void)mod_mask;
#endif
}

/**
 * Poll whether the global hotkey fired since the last call.
 * Returns 1 and clears the flag, or 0 if not fired.
 *
 * GTK4 approach: since GDK event filters were removed, we drain grabbed
 * KeyPress events directly from the X11 queue via XCheckMaskEvent().
 * This is safe to call every frame from the draw/idle callback.
 */
int64_t nitty_global_hotkey_poll(void)
{
#ifdef GDK_WINDOWING_X11
    if (g_global_hotkey_keycode != 0) {
        GdkDisplay *gdk_dpy = gdk_display_get_default();
        if (gdk_dpy && GDK_IS_X11_DISPLAY(gdk_dpy)) {
            Display *xdpy = gdk_x11_display_get_xdisplay(gdk_dpy);
            if (xdpy) {
                XEvent xe;
                /* Drain all pending KeyPress events from the X11 queue */
                while (XCheckMaskEvent(xdpy, KeyPressMask, &xe)) {
                    if (xe.type == KeyPress) {
                        XKeyEvent *ke = (XKeyEvent *)&xe;
                        if ((int)ke->keycode == g_global_hotkey_keycode) {
                            unsigned int clean = ke->state & ~(Mod2Mask | LockMask);
                            if (clean == g_global_hotkey_modmask) {
                                g_global_hotkey_fired = 1;
                                /* Drain any remaining events for this same key */
                            }
                        }
                    }
                }
            }
        }
    }
#endif

    if (g_global_hotkey_fired) {
        g_global_hotkey_fired = 0;
        return 1;
    }
    return 0;
}

/* ── Quake mode window management ──────────────────────────────────────── */

/**
 * Configure window hints for quake mode:
 * - Remove window decorations (title bar, borders)
 * - Skip taskbar and pager (X11 only via GdkSurface API)
 *
 * Must be called AFTER the GtkWindow is realized (i.e., after gtk_window_present).
 * GTK4 removed gtk_window_set_keep_above / gtk_window_set_skip_taskbar_hint.
 * Use the GdkSurface X11 backend equivalents instead.
 */
void nitty_quake_setup_window(int64_t win_ptr)
{
    GtkWindow *win = (GtkWindow *)(intptr_t)win_ptr;
    if (!win) return;

    /* Remove window decorations (works on all backends) */
    gtk_window_set_decorated(win, FALSE);

#ifdef GDK_WINDOWING_X11
    /* X11-specific: skip taskbar and pager via GdkSurface */
    GdkDisplay *gdk_dpy = gdk_display_get_default();
    if (gdk_dpy && GDK_IS_X11_DISPLAY(gdk_dpy)) {
        GtkNative  *native  = GTK_NATIVE(win);
        GdkSurface *surface = gtk_native_get_surface(native);
        if (surface) {
            gdk_x11_surface_set_skip_taskbar_hint(surface, TRUE);
            gdk_x11_surface_set_skip_pager_hint(surface, TRUE);
        }
    }
#endif
}

/**
 * Move the quake window to screen position (x, y).
 * X11: uses XMoveWindow for precise positioning.
 * Wayland: no-op — position is controlled by the compositor.
 */
void nitty_quake_move_window(int64_t win_ptr, int64_t x, int64_t y)
{
#ifdef GDK_WINDOWING_X11
    GdkDisplay *gdk_dpy = gdk_display_get_default();
    if (!gdk_dpy || !GDK_IS_X11_DISPLAY(gdk_dpy)) return;

    GtkWindow  *win     = (GtkWindow *)(intptr_t)win_ptr;
    if (!win) return;

    GtkNative  *native  = GTK_NATIVE(win);
    GdkSurface *surface = gtk_native_get_surface(native);
    if (!surface) return;

    Display *xdpy = gdk_x11_display_get_xdisplay(gdk_dpy);
    Window   xwin = gdk_x11_surface_get_xid(surface);

    XMoveWindow(xdpy, xwin, (int)x, (int)y);
    XSync(xdpy, False);
#else
    (void)win_ptr; (void)x; (void)y;
#endif
}

/**
 * Get the primary monitor width in pixels.
 */
int64_t nitty_quake_get_monitor_w(void)
{
    GdkDisplay *dpy = gdk_display_get_default();
    if (!dpy) return 1920;
    GListModel *monitors = gdk_display_get_monitors(dpy);
    if (!monitors || g_list_model_get_n_items(monitors) == 0) return 1920;
    GdkMonitor *mon = GDK_MONITOR(g_list_model_get_item(monitors, 0));
    if (!mon) return 1920;
    GdkRectangle geo;
    gdk_monitor_get_geometry(mon, &geo);
    g_object_unref(mon);
    return (int64_t)geo.width;
}

/**
 * Get the primary monitor height in pixels.
 */
int64_t nitty_quake_get_monitor_h(void)
{
    GdkDisplay *dpy = gdk_display_get_default();
    if (!dpy) return 1080;
    GListModel *monitors = gdk_display_get_monitors(dpy);
    if (!monitors || g_list_model_get_n_items(monitors) == 0) return 1080;
    GdkMonitor *mon = GDK_MONITOR(g_list_model_get_item(monitors, 0));
    if (!mon) return 1080;
    GdkRectangle geo;
    gdk_monitor_get_geometry(mon, &geo);
    g_object_unref(mon);
    return (int64_t)geo.height;
}


/* ═══════════════════════════════════════════════════════════════════════════
 * v0.6.5: Settings window
 *
 * A non-blocking, non-modal GTK4 settings window with a GtkNotebook.
 * Because Nitpick cannot register GTK signal callbacks, the window uses
 * the polling model:  C-side captures button clicks and stores a result,
 * Nitpick polls nitty_settings_poll_event() each frame.
 *
 * Events: 0=none, 1=Apply, 2=OK (also closes window), 3=Cancel (also closes)
 * ═══════════════════════════════════════════════════════════════════════════ */

/* ── Stored values ── */
static char   g_sv_font_family[256]  = "Monospace";
static int    g_sv_font_size         = 12;
static int    g_sv_scrollback        = 3000;
static char   g_sv_shell[512]        = "";
static char   g_sv_cursor_style[32]  = "block";
static int    g_sv_cursor_blink      = 1;
static int    g_sv_columns           = 80;
static int    g_sv_rows              = 24;
static char   g_sv_theme[128]        = "default";
static int    g_sv_opacity           = 1000;
static int    g_sv_close_on_exit     = 1;
static int    g_sv_confirm_close     = 1;

/* ── Window state ── */
static GtkWidget *g_settings_win   = NULL;
static int        g_settings_event = 0;

/* ── Widget pointers ── */
static GtkWidget *g_sw_font_family_entry  = NULL;
static GtkWidget *g_sw_font_size_spin     = NULL;
static GtkWidget *g_sw_scrollback_spin    = NULL;
static GtkWidget *g_sw_shell_entry        = NULL;
static GtkWidget *g_sw_cursor_style_drop  = NULL;
static GtkWidget *g_sw_cursor_blink_sw    = NULL;
static GtkWidget *g_sw_columns_spin       = NULL;
static GtkWidget *g_sw_rows_spin          = NULL;
static GtkWidget *g_sw_theme_drop         = NULL;
static GtkWidget *g_sw_opacity_scale      = NULL;
static GtkWidget *g_sw_close_on_exit_sw   = NULL;
static GtkWidget *g_sw_confirm_close_sw   = NULL;

/* ── Theme list ── */
static GtkStringList *g_sw_theme_list = NULL;
static char g_sv_theme_names[32][128];
static int  g_sv_theme_count = 0;

/* ── Cursor styles ── */
static const char *g_cursor_styles[] = { "block", "underline", "bar", NULL };

static void _sw_snapshot_values(void)
{
    if (g_sw_font_family_entry) {
        const char *t = gtk_editable_get_text(GTK_EDITABLE(g_sw_font_family_entry));
        if (t) { strncpy(g_sv_font_family, t, sizeof(g_sv_font_family)-1); g_sv_font_family[sizeof(g_sv_font_family)-1]='\0'; }
    }
    if (g_sw_font_size_spin)
        g_sv_font_size = (int)gtk_spin_button_get_value(GTK_SPIN_BUTTON(g_sw_font_size_spin));
    if (g_sw_scrollback_spin)
        g_sv_scrollback = (int)gtk_spin_button_get_value(GTK_SPIN_BUTTON(g_sw_scrollback_spin));
    if (g_sw_shell_entry) {
        const char *t = gtk_editable_get_text(GTK_EDITABLE(g_sw_shell_entry));
        if (t) { strncpy(g_sv_shell, t, sizeof(g_sv_shell)-1); g_sv_shell[sizeof(g_sv_shell)-1]='\0'; }
    }
    if (g_sw_cursor_style_drop) {
        guint sel = gtk_drop_down_get_selected(GTK_DROP_DOWN(g_sw_cursor_style_drop));
        if (sel == GTK_INVALID_LIST_POSITION || sel >= 3) sel = 0;
        strncpy(g_sv_cursor_style, g_cursor_styles[sel], sizeof(g_sv_cursor_style)-1);
        g_sv_cursor_style[sizeof(g_sv_cursor_style)-1] = '\0';
    }
    if (g_sw_cursor_blink_sw)
        g_sv_cursor_blink = gtk_switch_get_active(GTK_SWITCH(g_sw_cursor_blink_sw)) ? 1 : 0;
    if (g_sw_columns_spin)
        g_sv_columns = (int)gtk_spin_button_get_value(GTK_SPIN_BUTTON(g_sw_columns_spin));
    if (g_sw_rows_spin)
        g_sv_rows = (int)gtk_spin_button_get_value(GTK_SPIN_BUTTON(g_sw_rows_spin));
    if (g_sw_theme_drop && g_sv_theme_count > 0) {
        guint sel = gtk_drop_down_get_selected(GTK_DROP_DOWN(g_sw_theme_drop));
        if (sel != GTK_INVALID_LIST_POSITION && sel < (guint)g_sv_theme_count) {
            strncpy(g_sv_theme, g_sv_theme_names[sel], sizeof(g_sv_theme)-1);
            g_sv_theme[sizeof(g_sv_theme)-1] = '\0';
        }
    }
    if (g_sw_opacity_scale) {
        double val = gtk_range_get_value(GTK_RANGE(g_sw_opacity_scale));
        g_sv_opacity = (int)(val * 1000.0);
        if (g_sv_opacity < 100) g_sv_opacity = 100;
        if (g_sv_opacity > 1000) g_sv_opacity = 1000;
    }
    if (g_sw_close_on_exit_sw)
        g_sv_close_on_exit = gtk_switch_get_active(GTK_SWITCH(g_sw_close_on_exit_sw)) ? 1 : 0;
    if (g_sw_confirm_close_sw)
        g_sv_confirm_close = gtk_switch_get_active(GTK_SWITCH(g_sw_confirm_close_sw)) ? 1 : 0;
}

static void _sw_on_apply(GtkButton *btn, gpointer ud)  { (void)btn;(void)ud; _sw_snapshot_values(); g_settings_event=1; }
static void _sw_on_ok(GtkButton *btn, gpointer ud)     { (void)btn;(void)ud; _sw_snapshot_values(); g_settings_event=2; if(g_settings_win) gtk_window_close(GTK_WINDOW(g_settings_win)); }
static void _sw_on_cancel(GtkButton *btn, gpointer ud) { (void)btn;(void)ud; g_settings_event=3; if(g_settings_win) gtk_window_close(GTK_WINDOW(g_settings_win)); }

static void _sw_on_destroy(GtkWidget *w, gpointer ud)
{
    (void)w;(void)ud;
    g_settings_win=NULL;
    g_sw_font_family_entry=NULL; g_sw_font_size_spin=NULL; g_sw_scrollback_spin=NULL;
    g_sw_shell_entry=NULL; g_sw_cursor_style_drop=NULL; g_sw_cursor_blink_sw=NULL;
    g_sw_columns_spin=NULL; g_sw_rows_spin=NULL; g_sw_theme_drop=NULL;
    g_sw_opacity_scale=NULL; g_sw_close_on_exit_sw=NULL; g_sw_confirm_close_sw=NULL;
    g_sw_theme_list=NULL;
}

static GtkWidget *_sw_row(const char *lbl_text, GtkWidget *widget)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_start(row, 4); gtk_widget_set_margin_end(row, 4);
    GtkWidget *lbl = gtk_label_new(lbl_text);
    gtk_widget_set_halign(lbl, GTK_ALIGN_START);
    gtk_widget_set_hexpand(lbl, TRUE);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
    gtk_widget_set_halign(widget, GTK_ALIGN_END);
    gtk_widget_set_valign(widget, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(row), lbl);
    gtk_box_append(GTK_BOX(row), widget);
    return row;
}

static GtkWidget *_sw_section_label(const char *text)
{
    GtkWidget *lbl = gtk_label_new(NULL);
    char markup[256];
    snprintf(markup, sizeof(markup), "<b>%s</b>", text);
    gtk_label_set_markup(GTK_LABEL(lbl), markup);
    gtk_widget_set_halign(lbl, GTK_ALIGN_START);
    gtk_widget_set_margin_top(lbl, 8);
    gtk_widget_set_margin_bottom(lbl, 4);
    gtk_widget_set_margin_start(lbl, 4);
    return lbl;
}

static int _sw_find_theme_index(const char *name)
{
    for (int i=0; i<g_sv_theme_count; i++)
        if (strcmp(g_sv_theme_names[i], name)==0) return i;
    return 0;
}

static int _sw_find_cursor_style_index(const char *style)
{
    for (int i=0; g_cursor_styles[i]!=NULL; i++)
        if (strcmp(g_cursor_styles[i], style)==0) return i;
    return 0;
}

static GtkWidget *_sw_build_general_tab(void)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(box,12); gtk_widget_set_margin_end(box,12);
    gtk_widget_set_margin_top(box,12);  gtk_widget_set_margin_bottom(box,12);
    gtk_box_append(GTK_BOX(box), _sw_section_label("Behavior"));
    g_sw_close_on_exit_sw = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(g_sw_close_on_exit_sw), g_sv_close_on_exit ? TRUE : FALSE);
    gtk_box_append(GTK_BOX(box), _sw_row("Close tab when shell exits", g_sw_close_on_exit_sw));
    g_sw_confirm_close_sw = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(g_sw_confirm_close_sw), g_sv_confirm_close ? TRUE : FALSE);
    gtk_box_append(GTK_BOX(box), _sw_row("Confirm before closing a tab", g_sw_confirm_close_sw));
    return box;
}

static GtkWidget *_sw_build_appearance_tab(void)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(box,12); gtk_widget_set_margin_end(box,12);
    gtk_widget_set_margin_top(box,12);  gtk_widget_set_margin_bottom(box,12);
    gtk_box_append(GTK_BOX(box), _sw_section_label("Color Theme"));
    g_sw_theme_list = gtk_string_list_new(NULL);
    for (int i=0; i<g_sv_theme_count; i++)
        gtk_string_list_append(g_sw_theme_list, g_sv_theme_names[i]);
    g_sw_theme_drop = gtk_drop_down_new(G_LIST_MODEL(g_sw_theme_list), NULL);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_sw_theme_drop), (guint)_sw_find_theme_index(g_sv_theme));
    gtk_widget_set_size_request(g_sw_theme_drop, 200, -1);
    gtk_box_append(GTK_BOX(box), _sw_row("Theme", g_sw_theme_drop));
    gtk_box_append(GTK_BOX(box), _sw_section_label("Window"));
    g_sw_opacity_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.1, 1.0, 0.05);
    gtk_range_set_value(GTK_RANGE(g_sw_opacity_scale), (double)g_sv_opacity/1000.0);
    gtk_widget_set_size_request(g_sw_opacity_scale, 180, -1);
    gtk_scale_set_draw_value(GTK_SCALE(g_sw_opacity_scale), TRUE);
    gtk_scale_set_digits(GTK_SCALE(g_sw_opacity_scale), 2);
    gtk_box_append(GTK_BOX(box), _sw_row("Opacity", g_sw_opacity_scale));
    return box;
}

static GtkWidget *_sw_build_terminal_tab(void)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(box,12); gtk_widget_set_margin_end(box,12);
    gtk_widget_set_margin_top(box,12);  gtk_widget_set_margin_bottom(box,12);
    gtk_box_append(GTK_BOX(box), _sw_section_label("Font"));
    g_sw_font_family_entry = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(g_sw_font_family_entry), g_sv_font_family);
    gtk_widget_set_size_request(g_sw_font_family_entry, 200, -1);
    gtk_box_append(GTK_BOX(box), _sw_row("Font Family", g_sw_font_family_entry));
    g_sw_font_size_spin = gtk_spin_button_new_with_range(6.0, 72.0, 1.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(g_sw_font_size_spin), (double)g_sv_font_size);
    gtk_box_append(GTK_BOX(box), _sw_row("Font Size", g_sw_font_size_spin));
    gtk_box_append(GTK_BOX(box), _sw_section_label("Display"));
    g_sw_columns_spin = gtk_spin_button_new_with_range(40.0, 400.0, 1.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(g_sw_columns_spin), (double)g_sv_columns);
    gtk_box_append(GTK_BOX(box), _sw_row("Columns", g_sw_columns_spin));
    g_sw_rows_spin = gtk_spin_button_new_with_range(8.0, 200.0, 1.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(g_sw_rows_spin), (double)g_sv_rows);
    gtk_box_append(GTK_BOX(box), _sw_row("Rows", g_sw_rows_spin));
    g_sw_scrollback_spin = gtk_spin_button_new_with_range(0.0, 1000000.0, 1000.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(g_sw_scrollback_spin), (double)g_sv_scrollback);
    gtk_box_append(GTK_BOX(box), _sw_row("Scrollback Lines", g_sw_scrollback_spin));
    gtk_box_append(GTK_BOX(box), _sw_section_label("Cursor"));
    GtkStringList *csl = gtk_string_list_new(NULL);
    gtk_string_list_append(csl, "block");
    gtk_string_list_append(csl, "underline");
    gtk_string_list_append(csl, "bar");
    g_sw_cursor_style_drop = gtk_drop_down_new(G_LIST_MODEL(csl), NULL);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_sw_cursor_style_drop),
                               (guint)_sw_find_cursor_style_index(g_sv_cursor_style));
    gtk_box_append(GTK_BOX(box), _sw_row("Cursor Style", g_sw_cursor_style_drop));
    g_sw_cursor_blink_sw = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(g_sw_cursor_blink_sw), g_sv_cursor_blink ? TRUE : FALSE);
    gtk_box_append(GTK_BOX(box), _sw_row("Cursor Blink", g_sw_cursor_blink_sw));
    gtk_box_append(GTK_BOX(box), _sw_section_label("Shell"));
    g_sw_shell_entry = gtk_entry_new();
    if (g_sv_shell[0])
        gtk_editable_set_text(GTK_EDITABLE(g_sw_shell_entry), g_sv_shell);
    else
        gtk_entry_set_placeholder_text(GTK_ENTRY(g_sw_shell_entry), "Default ($SHELL)");
    gtk_widget_set_size_request(g_sw_shell_entry, 200, -1);
    gtk_box_append(GTK_BOX(box), _sw_row("Shell Binary", g_sw_shell_entry));
    return box;
}

static GtkWidget *_sw_build_stub_tab(const char *msg)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(box,24); gtk_widget_set_margin_end(box,24);
    gtk_widget_set_margin_top(box,32);  gtk_widget_set_margin_bottom(box,24);
    GtkWidget *lbl = gtk_label_new(msg);
    gtk_widget_set_halign(lbl, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(lbl, GTK_ALIGN_CENTER);
    gtk_label_set_wrap(GTK_LABEL(lbl), TRUE);
    gtk_box_append(GTK_BOX(box), lbl);
    return box;
}

int64_t nitty_settings_open(int64_t parent_win_ptr)
{
    if (g_settings_win != NULL) { gtk_window_present(GTK_WINDOW(g_settings_win)); return 1; }

    GtkWidget *win = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(win), "Nitty Settings");
    gtk_window_set_default_size(GTK_WINDOW(win), 540, 480);
    gtk_window_set_resizable(GTK_WINDOW(win), TRUE);
    gtk_window_set_modal(GTK_WINDOW(win), FALSE);

    if (parent_win_ptr != 0)
        gtk_window_set_transient_for(GTK_WINDOW(win), GTK_WINDOW((GtkWidget *)(uintptr_t)parent_win_ptr));
    else if (g_main_window)
        gtk_window_set_transient_for(GTK_WINDOW(win), GTK_WINDOW(g_main_window));

    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(GTK_WINDOW(win), outer);

    GtkWidget *nb = gtk_notebook_new();
    gtk_widget_set_hexpand(nb, TRUE);
    gtk_widget_set_vexpand(nb, TRUE);
    gtk_box_append(GTK_BOX(outer), nb);

    gtk_notebook_append_page(GTK_NOTEBOOK(nb), _sw_build_general_tab(),    gtk_label_new("General"));
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), _sw_build_appearance_tab(), gtk_label_new("Appearance"));
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), _sw_build_terminal_tab(),   gtk_label_new("Terminal"));
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), _sw_build_stub_tab("Profile editor — coming in v0.6.6"), gtk_label_new("Profiles"));
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), _sw_build_stub_tab("Hotkey editor — coming in v0.6.6"), gtk_label_new("Hotkeys"));
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), _sw_build_stub_tab("Plugin system — coming in v0.7.x"),  gtk_label_new("Plugins"));

    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_append(GTK_BOX(outer), sep);

    GtkWidget *btn_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(btn_bar,12); gtk_widget_set_margin_end(btn_bar,12);
    gtk_widget_set_margin_top(btn_bar,8);   gtk_widget_set_margin_bottom(btn_bar,8);
    gtk_box_append(GTK_BOX(outer), btn_bar);

    GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(spacer, TRUE);
    gtk_box_append(GTK_BOX(btn_bar), spacer);

    GtkWidget *cancel_btn = gtk_button_new_with_label("Cancel");
    GtkWidget *apply_btn  = gtk_button_new_with_label("Apply");
    GtkWidget *ok_btn     = gtk_button_new_with_label("OK");
    gtk_widget_add_css_class(ok_btn, "suggested-action");

    g_signal_connect(cancel_btn, "clicked", G_CALLBACK(_sw_on_cancel), NULL);
    g_signal_connect(apply_btn,  "clicked", G_CALLBACK(_sw_on_apply),  NULL);
    g_signal_connect(ok_btn,     "clicked", G_CALLBACK(_sw_on_ok),     NULL);

    gtk_box_append(GTK_BOX(btn_bar), cancel_btn);
    gtk_box_append(GTK_BOX(btn_bar), apply_btn);
    gtk_box_append(GTK_BOX(btn_bar), ok_btn);

    g_signal_connect(win, "destroy", G_CALLBACK(_sw_on_destroy), NULL);

    g_settings_win = win;
    gtk_widget_set_visible(win, TRUE);
    return 1;
}

void        nitty_settings_close(void)       { if (g_settings_win) gtk_window_close(GTK_WINDOW(g_settings_win)); }
int64_t     nitty_settings_is_open(void)     { return g_settings_win != NULL ? 1 : 0; }
int64_t     nitty_settings_poll_event(void)  { int ev=g_settings_event; g_settings_event=0; return (int64_t)ev; }

void nitty_settings_init_values(
    const char *font_family, int64_t font_size,
    int64_t scrollback, const char *shell,
    const char *cursor_style, int64_t cursor_blink,
    int64_t columns, int64_t rows,
    const char *theme, int64_t opacity,
    int64_t close_on_exit, int64_t confirm_close)
{
    if (font_family)  { strncpy(g_sv_font_family,   font_family,   sizeof(g_sv_font_family)-1);   g_sv_font_family[sizeof(g_sv_font_family)-1]='\0'; }
    if (font_size>0)   g_sv_font_size   = (int)font_size;
    if (scrollback>=0) g_sv_scrollback  = (int)scrollback;
    if (shell)        { strncpy(g_sv_shell,          shell,         sizeof(g_sv_shell)-1);          g_sv_shell[sizeof(g_sv_shell)-1]='\0'; }
    if (cursor_style) { strncpy(g_sv_cursor_style,   cursor_style,  sizeof(g_sv_cursor_style)-1);   g_sv_cursor_style[sizeof(g_sv_cursor_style)-1]='\0'; }
    g_sv_cursor_blink  = (int)cursor_blink;
    if (columns>0)     g_sv_columns = (int)columns;
    if (rows>0)        g_sv_rows    = (int)rows;
    if (theme)        { strncpy(g_sv_theme,          theme,         sizeof(g_sv_theme)-1);          g_sv_theme[sizeof(g_sv_theme)-1]='\0'; }
    if (opacity>0)     g_sv_opacity = (int)opacity;
    g_sv_close_on_exit = (int)close_on_exit;
    g_sv_confirm_close = (int)confirm_close;
}

void nitty_settings_clear_themes(void) { g_sv_theme_count=0; memset(g_sv_theme_names,0,sizeof(g_sv_theme_names)); }

void nitty_settings_add_theme(const char *name)
{
    if (!name || g_sv_theme_count>=32) return;
    strncpy(g_sv_theme_names[g_sv_theme_count], name, 127);
    g_sv_theme_names[g_sv_theme_count][127]='\0';
    g_sv_theme_count++;
}

const char *nitty_settings_get_font_family(void)  { return g_sv_font_family; }
int64_t     nitty_settings_get_font_size(void)    { return (int64_t)g_sv_font_size; }
int64_t     nitty_settings_get_scrollback(void)   { return (int64_t)g_sv_scrollback; }
const char *nitty_settings_get_shell(void)        { return g_sv_shell; }
const char *nitty_settings_get_cursor_style(void) { return g_sv_cursor_style; }
int64_t     nitty_settings_get_cursor_blink(void) { return (int64_t)g_sv_cursor_blink; }
int64_t     nitty_settings_get_columns(void)      { return (int64_t)g_sv_columns; }
int64_t     nitty_settings_get_rows(void)         { return (int64_t)g_sv_rows; }
const char *nitty_settings_get_theme(void)        { return g_sv_theme; }
int64_t     nitty_settings_get_opacity(void)      { return (int64_t)g_sv_opacity; }
int64_t     nitty_settings_get_close_on_exit(void){ return (int64_t)g_sv_close_on_exit; }
int64_t     nitty_settings_get_confirm_close(void){ return (int64_t)g_sv_confirm_close; }

/* ═══════════════════════════════════════════════════════════════════════════
 * v0.6.5: Config file watcher (GFileMonitor-based)
 * ═══════════════════════════════════════════════════════════════════════════ */

static GFileMonitor *g_config_monitor    = NULL;
static int           g_config_changed    = 0;
static int64_t       g_config_changed_ms = 0;

static void _on_config_file_changed(GFileMonitor *monitor, GFile *file, GFile *other,
                                    GFileMonitorEvent ev, gpointer user_data)
{
    (void)monitor;(void)file;(void)other;(void)user_data;
    if (ev==G_FILE_MONITOR_EVENT_CHANGED ||
        ev==G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT ||
        ev==G_FILE_MONITOR_EVENT_CREATED) {
        g_config_changed    = 1;
        g_config_changed_ms = g_get_monotonic_time()/1000;
    }
}

int64_t nitty_gtk4_config_watch_start(const char *path)
{
    if (!path||!path[0]) return -1;
    if (g_config_monitor) { g_object_unref(g_config_monitor); g_config_monitor=NULL; }
    GFile *file = g_file_new_for_path(path);
    GError *err = NULL;
    g_config_monitor = g_file_monitor_file(file, G_FILE_MONITOR_NONE, NULL, &err);
    g_object_unref(file);
    if (err) { fprintf(stderr,"nitty: config_watch_start: %s\n",err->message); g_error_free(err); return -1; }
    g_signal_connect(g_config_monitor, "changed", G_CALLBACK(_on_config_file_changed), NULL);
    g_config_changed=0; g_config_changed_ms=0;
    return 0;
}

int64_t nitty_gtk4_config_watch_poll(void)
{
    if (!g_config_changed) return 0;
    int64_t now_ms = g_get_monotonic_time()/1000;
    if (now_ms - g_config_changed_ms < 200) return 0;
    g_config_changed=0;
    return 1;
}

void nitty_gtk4_config_watch_stop(void)
{
    if (g_config_monitor) {
        g_file_monitor_cancel(g_config_monitor);
        g_object_unref(g_config_monitor);
        g_config_monitor=NULL;
    }
    g_config_changed=0;
}

/* ═══════════════════════════════════════════════════════════════════════
 * v0.7.0: Search bar — drawn overlay + key event queue
 * ═══════════════════════════════════════════════════════════════════════ */

/* Active flag — checked by nitty_input.c before PTY routing */
static int g_search_bar_active = 0;

/* Search event ring buffer (capacity 32) */
#define SEARCH_EVQ_CAP 32
static int64_t g_search_evq_type[SEARCH_EVQ_CAP];
static int64_t g_search_evq_char[SEARCH_EVQ_CAP];
static int     g_search_evq_head = 0;  /* next read */
static int     g_search_evq_tail = 0;  /* next write */
static int64_t g_search_last_char = 0;

/* ── Active flag API ─────────────────────────────────────────────────── */

int nitty_search_bar_is_active(void)
{
    return g_search_bar_active;
}

void nitty_search_bar_set_active(int active)
{
    g_search_bar_active = active ? 1 : 0;
    if (!active) {
        /* Flush event queue on close */
        g_search_evq_head = 0;
        g_search_evq_tail = 0;
    }
}

/* ── Key interception ────────────────────────────────────────────────── */

static void _search_evq_push(int64_t ev_type, int64_t ev_char)
{
    int next = (g_search_evq_tail + 1) % SEARCH_EVQ_CAP;
    if (next == g_search_evq_head) return; /* queue full — drop */
    g_search_evq_type[g_search_evq_tail] = ev_type;
    g_search_evq_char[g_search_evq_tail] = ev_char;
    g_search_evq_tail = next;
}

int nitty_search_intercept_key(guint keyval, guint state)
{
    int has_shift = (state & GDK_SHIFT_MASK) != 0;
    int has_ctrl  = (state & GDK_CONTROL_MASK) != 0;

    /* Escape: close search */
    if (keyval == GDK_KEY_Escape) {
        _search_evq_push(3, 0);
        return 1;
    }

    /* Backspace */
    if (keyval == GDK_KEY_BackSpace) {
        _search_evq_push(2, 0);
        return 1;
    }

    /* Enter: next match (Shift+Enter: prev match) */
    if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) {
        if (has_shift) {
            _search_evq_push(5, 0);  /* prev */
        } else {
            _search_evq_push(4, 0);  /* next */
        }
        return 1;
    }

    /* Ctrl+Shift+C: toggle case sensitivity */
    if (has_ctrl && has_shift && (keyval == GDK_KEY_c || keyval == GDK_KEY_C)) {
        _search_evq_push(6, 0);
        return 1;
    }

    /* Arrow keys / function keys: let them pass through (allow scrolling in search mode) */
    if (keyval == GDK_KEY_Up || keyval == GDK_KEY_Down ||
        keyval == GDK_KEY_Left || keyval == GDK_KEY_Right ||
        keyval == GDK_KEY_Page_Up || keyval == GDK_KEY_Page_Down)
    {
        return 0;  /* not consumed: let normal scroll handling occur */
    }

    /* Printable characters (no Ctrl modifier) */
    if (!has_ctrl && keyval >= 0x20 && keyval <= 0x10FFFF) {
        _search_evq_push(1, (int64_t)keyval);
        return 1;
    }

    /* Everything else: consume but don't enqueue (don't write to PTY) */
    if (keyval >= 0x20) return 1;
    return 0;
}

int64_t nitty_search_event_poll(void)
{
    if (g_search_evq_head == g_search_evq_tail) return 0;
    int64_t ev   = g_search_evq_type[g_search_evq_head];
    g_search_last_char = g_search_evq_char[g_search_evq_head];
    g_search_evq_head = (g_search_evq_head + 1) % SEARCH_EVQ_CAP;
    return ev;
}

int64_t nitty_search_event_get_char(void)
{
    return g_search_last_char;
}

/* ── Drawn search bar ────────────────────────────────────────────────── */

/*
 * g_draw_cr is defined in this file (nitty_gtk4_shim.c) and used by
 * nitty_render.c via extern. Use it directly here.
 */
extern int64_t nitty_render_get_tab_bar_height(void);

void nitty_search_bar_draw(const char *query, const char *match_info,
                            int is_visible, int case_on)
{
    if (!is_visible) return;

    cairo_t *cr = g_draw_cr;
    if (cr == NULL) return;

    int64_t win_w     = nitty_gtk4_get_draw_width();
    int64_t tab_bar_h = nitty_render_get_tab_bar_height();

    /* Search bar dimensions */
    double bar_w = 360.0;
    double bar_h = 38.0;
    double bar_x = (double)(win_w) - bar_w - 12.0;
    double bar_y = (double)(tab_bar_h) + 8.0;
    double radius = 6.0;

    /* ── Save state so we don't disturb the tab-bar translation ── */
    cairo_save(cr);

    /* Draw rounded-rectangle background: dark semi-transparent */
    cairo_new_path(cr);
    cairo_arc(cr, bar_x + bar_w - radius, bar_y + radius,           radius, -G_PI_2, 0.0);
    cairo_arc(cr, bar_x + bar_w - radius, bar_y + bar_h - radius,   radius, 0.0,     G_PI_2);
    cairo_arc(cr, bar_x + radius,         bar_y + bar_h - radius,   radius, G_PI_2,  G_PI);
    cairo_arc(cr, bar_x + radius,         bar_y + radius,           radius, G_PI,    3.0 * G_PI_2);
    cairo_close_path(cr);
    cairo_set_source_rgba(cr, 0.12, 0.12, 0.14, 0.94);
    cairo_fill(cr);

    /* Thin border */
    cairo_new_path(cr);
    cairo_arc(cr, bar_x + bar_w - radius, bar_y + radius,           radius, -G_PI_2, 0.0);
    cairo_arc(cr, bar_x + bar_w - radius, bar_y + bar_h - radius,   radius, 0.0,     G_PI_2);
    cairo_arc(cr, bar_x + radius,         bar_y + bar_h - radius,   radius, G_PI_2,  G_PI);
    cairo_arc(cr, bar_x + radius,         bar_y + radius,           radius, G_PI,    3.0 * G_PI_2);
    cairo_close_path(cr);
    cairo_set_source_rgba(cr, 0.4, 0.4, 0.45, 0.7);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    /* ── Query text ── */
    PangoLayout *lo = pango_cairo_create_layout(cr);
    PangoFontDescription *fd = pango_font_description_from_string("Sans 11");
    pango_layout_set_font_description(lo, fd);

    /* Query field background (slightly lighter) */
    double qx = bar_x + 8.0;
    double qy = bar_y + 6.0;
    double qw = bar_w - 100.0;
    double qh = bar_h - 12.0;
    cairo_set_source_rgba(cr, 0.22, 0.22, 0.25, 1.0);
    cairo_rectangle(cr, qx, qy, qw, qh);
    cairo_fill(cr);

    /* Query text */
    const char *q_text = (query && query[0]) ? query : "";
    pango_layout_set_text(lo, q_text[0] ? q_text : " ", -1);
    cairo_set_source_rgb(cr, 0.92, 0.92, 0.92);
    cairo_move_to(cr, qx + 4.0, qy + 4.0);
    pango_cairo_show_layout(cr, lo);

    /* Cursor blink placeholder: a simple | after query text */
    int q_w_px = 0, q_h_px = 0;
    pango_layout_get_pixel_size(lo, &q_w_px, &q_h_px);
    cairo_set_source_rgba(cr, 0.85, 0.85, 0.85, 0.9);
    cairo_rectangle(cr, qx + 4.0 + (double)q_w_px + 1.0, qy + 4.0, 1.5, (double)q_h_px);
    cairo_fill(cr);

    /* ── Match info label ── */
    double lx = bar_x + qw + 14.0;
    double lw = bar_w - qw - 22.0;
    if (match_info && match_info[0]) {
        /* Red tint if "No matches" */
        int no_match = (match_info[0] == 'N' && match_info[1] == 'o');
        if (no_match) {
            cairo_set_source_rgb(cr, 0.9, 0.35, 0.35);
        } else {
            cairo_set_source_rgb(cr, 0.65, 0.65, 0.70);
        }
        pango_layout_set_text(lo, match_info, -1);
        pango_layout_set_width(lo, (int)(lw * PANGO_SCALE));
        pango_layout_set_ellipsize(lo, PANGO_ELLIPSIZE_END);
        cairo_move_to(cr, lx, bar_y + 10.0);
        pango_cairo_show_layout(cr, lo);
    }

    /* ── Case-sensitive indicator (small "Aa" badge) ── */
    double cx_btn = bar_x + bar_w - 26.0;
    double cy_btn = bar_y + 6.0;
    if (case_on) {
        cairo_set_source_rgba(cr, 0.25, 0.55, 0.95, 0.85);
        cairo_rectangle(cr, cx_btn, cy_btn, 20.0, 18.0);
        cairo_fill(cr);
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    } else {
        cairo_set_source_rgba(cr, 0.30, 0.30, 0.34, 0.9);
        cairo_rectangle(cr, cx_btn, cy_btn, 20.0, 18.0);
        cairo_fill(cr);
        cairo_set_source_rgb(cr, 0.60, 0.60, 0.65);
    }
    PangoFontDescription *fd_small = pango_font_description_from_string("Sans Bold 9");
    pango_layout_set_font_description(lo, fd_small);
    pango_layout_set_text(lo, "Aa", -1);
    pango_layout_set_width(lo, -1);
    cairo_move_to(cr, cx_btn + 2.0, cy_btn + 2.0);
    pango_cairo_show_layout(cr, lo);
    pango_font_description_free(fd_small);

    pango_font_description_free(fd);
    g_object_unref(lo);

    cairo_restore(cr);
}

/* ═══════════════════════════════════════════════════════════════════════
 * v0.7.2: Link opening and pointer cursor
 * ═══════════════════════════════════════════════════════════════════════ */

/* Open a URI string with the system default handler.
 * Uses GLib's g_app_info_launch_default_for_uri which is non-blocking and
 * GTK4-safe (does not conflict with the main loop's SIGCHLD handler).
 * Returns 1 on success, 0 on failure. */
int64_t nitty_gtk4_open_url(const char *url)
{
    if (!url || url[0] == '\0') return 0;
    GError *err = NULL;
    gboolean ok = g_app_info_launch_default_for_uri(url, NULL, &err);
    if (!ok) {
        if (err) {
            g_printerr("nitty: link open failed: %s\n", err->message);
            g_error_free(err);
        }
        return 0;
    }
    return 1;
}

/* Change the drawing area cursor to a pointer hand (is_pointer=1) or
 * the default cursor (is_pointer=0).  Called when hovering link cells. */
void nitty_gtk4_set_cursor_pointer(int64_t is_pointer)
{
    if (g_drawing_area == NULL) return;
    const char *name = (is_pointer != 0) ? "pointer" : "default";
    gtk_widget_set_cursor_from_name(g_drawing_area, name);
}

/* ═══════════════════════════════════════════════════════════════════════
 * v0.7.3: Window Features — opacity, fullscreen, maximize, decoration,
 *          icon name, always-on-top (X11), default size
 * ═══════════════════════════════════════════════════════════════════════ */

void nitty_gtk4_configure_opacity(int64_t opacity_fp1000)
{
    if (opacity_fp1000 < 0)    opacity_fp1000 = 0;
    if (opacity_fp1000 > 1000) opacity_fp1000 = 1000;
    g_cfg_opacity_fp1000 = opacity_fp1000;
    /* If window is already open, apply immediately */
    if (g_main_window != NULL) {
        double opacity = (double)opacity_fp1000 / 1000.0;
        gtk_widget_set_opacity(GTK_WIDGET(g_main_window), opacity);
    }
}

void nitty_gtk4_configure_default_size(int64_t width, int64_t height)
{
    g_cfg_default_width  = width;
    g_cfg_default_height = height;
    /* If window is already shown, set default size for next resize */
    if (g_main_window != NULL && width > 0 && height > 0) {
        gtk_window_set_default_size(GTK_WINDOW(g_main_window),
                                    (int)width, (int)height);
    }
}

void nitty_gtk4_window_fullscreen(int64_t win_ptr)
{
    if (win_ptr == 0) return;
    gtk_window_fullscreen(GTK_WINDOW((GtkWidget *)(uintptr_t)win_ptr));
}

void nitty_gtk4_window_unfullscreen(int64_t win_ptr)
{
    if (win_ptr == 0) return;
    gtk_window_unfullscreen(GTK_WINDOW((GtkWidget *)(uintptr_t)win_ptr));
}

int64_t nitty_gtk4_window_is_fullscreen(int64_t win_ptr)
{
    if (win_ptr == 0) return 0;
    return gtk_window_is_fullscreen(GTK_WINDOW((GtkWidget *)(uintptr_t)win_ptr)) ? 1 : 0;
}

void nitty_gtk4_window_maximize(int64_t win_ptr)
{
    if (win_ptr == 0) return;
    gtk_window_maximize(GTK_WINDOW((GtkWidget *)(uintptr_t)win_ptr));
}

void nitty_gtk4_window_unmaximize(int64_t win_ptr)
{
    if (win_ptr == 0) return;
    gtk_window_unmaximize(GTK_WINDOW((GtkWidget *)(uintptr_t)win_ptr));
}

int64_t nitty_gtk4_window_is_maximized(int64_t win_ptr)
{
    if (win_ptr == 0) return 0;
    return gtk_window_is_maximized(GTK_WINDOW((GtkWidget *)(uintptr_t)win_ptr)) ? 1 : 0;
}

void nitty_gtk4_window_set_decorated(int64_t win_ptr, int64_t decorated)
{
    if (win_ptr == 0) return;
    gtk_window_set_decorated(GTK_WINDOW((GtkWidget *)(uintptr_t)win_ptr),
                             decorated ? TRUE : FALSE);
}

void nitty_gtk4_window_set_icon_name(int64_t win_ptr, const char *name)
{
    if (win_ptr == 0 || name == NULL || name[0] == '\0') return;
    gtk_window_set_icon_name(GTK_WINDOW((GtkWidget *)(uintptr_t)win_ptr), name);
}

void nitty_gtk4_window_set_keep_above(int64_t win_ptr, int64_t keep_above)
{
    if (win_ptr == 0) return;
    GtkWidget *win = (GtkWidget *)(uintptr_t)win_ptr;

#ifdef GDK_WINDOWING_X11
    /* Get the underlying GdkSurface */
    GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(win));
    if (surface == NULL) return;

    /* Must be an X11 surface */
    if (!GDK_IS_X11_SURFACE(surface)) return;

    Display *dpy = gdk_x11_display_get_xdisplay(gdk_surface_get_display(surface));
    Window   xwin = gdk_x11_surface_get_xid(surface);

    Atom wm_state  = XInternAtom(dpy, "_NET_WM_STATE", False);
    Atom wm_above  = XInternAtom(dpy, "_NET_WM_STATE_ABOVE", False);

    XEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type                 = ClientMessage;
    ev.xclient.window       = xwin;
    ev.xclient.message_type = wm_state;
    ev.xclient.format       = 32;
    ev.xclient.data.l[0]    = keep_above ? 1 : 0;  /* 1=add, 0=remove */
    ev.xclient.data.l[1]    = (long)wm_above;
    ev.xclient.data.l[2]    = 0;
    ev.xclient.data.l[3]    = 1;  /* source: normal application */

    XSendEvent(dpy, DefaultRootWindow(dpy), False,
               SubstructureNotifyMask | SubstructureRedirectMask, &ev);
    XFlush(dpy);
#else
    /* Wayland: gtk-layer-shell would be needed; skip silently */
    (void)keep_above;
#endif
}

/* ═══════════════════════════════════════════════════════════════════════
 * v0.7.4: Bell and process completion notifications
 * ═══════════════════════════════════════════════════════════════════════ */

/* ── Audible bell ───────────────────────────────────────────────────────── */

void nitty_gtk4_display_beep(void)
{
    GdkDisplay *display = gdk_display_get_default();
    if (display != NULL) gdk_display_beep(display);
}

/* ── Process completion notifications ──────────────────────────────────── */

/* Per-slot shell start times (monotonic ms, set when pid is registered) */
static int64_t g_proc_start_ms[16]    = {0};
static int64_t g_proc_min_notify_ms   = 10000;    /* default: 10 s */
static char    g_proc_notify_msg[512] = "";        /* pending notification */
static int     g_proc_notify_pending  = 0;

/* Helper: current monotonic time in ms */
static int64_t _proc_now_ms(void)
{
    return (int64_t)(g_get_monotonic_time() / 1000LL);
}

/* Helper: read /proc/<pid>/comm into buf */
static void _proc_comm(int64_t pid, char *buf, int bufsz)
{
    buf[0] = '\0';
    if (pid <= 0) return;
    char path[64];
    snprintf(path, sizeof(path), "/proc/%lld/comm", (long long)pid);
    FILE *f = fopen(path, "r");
    if (!f) return;
    size_t n = fread(buf, 1, (size_t)(bufsz - 1), f);
    fclose(f);
    buf[n] = '\0';
    /* Strip trailing newline */
    while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r')) { buf[--n] = '\0'; }
}

void nitty_gtk4_proc_set_min_ms(int64_t ms)
{
    g_proc_min_notify_ms = ms;
}

/* Register a new shell PID for a tab slot (call after spawning).
 * Records the start time so we can compute duration on exit. */
void nitty_gtk4_proc_register(int32_t slot, int64_t pid)
{
    if (slot < 0 || slot >= 16) return;
    g_proc_start_ms[slot] = _proc_now_ms();
    /* Also update g_tab_pids so the existing completion poller tracks it */
    g_tab_pids[slot] = pid;
}

int32_t nitty_gtk4_proc_notify_poll(void)
{
    if (g_proc_notify_pending) {
        g_proc_notify_pending = 0;
        return 1;
    }

    /* Check each background tab for shell exit with sufficient runtime */
    for (int i = 0; i < 16; i++) {
        int64_t pid = g_tab_pids[i];
        if (pid <= 0) continue;
        /* Only notify for background tabs (not the active PTY) */
        if (pid == g_terminal_child_pid) continue;

        int status;
        pid_t r = waitpid((pid_t)pid, &status, WNOHANG);
        if (r != (pid_t)pid) continue;

        /* Shell exited — compute runtime */
        int64_t start = g_proc_start_ms[i];
        int64_t runtime_ms = (start > 0) ? (_proc_now_ms() - start) : 0;
        g_tab_pids[i] = -1;
        g_proc_start_ms[i] = 0;

        if (g_proc_min_notify_ms > 0 && runtime_ms < g_proc_min_notify_ms) {
            continue; /* Too short — skip notification */
        }

        /* Build message: "Process 'name' completed (tab N, Xm Ys)" */
        char comm[32] = "";
        _proc_comm(pid, comm, sizeof(comm));
        long long secs = (long long)(runtime_ms / 1000);
        long long mins = secs / 60;
        secs = secs % 60;
        if (mins > 0) {
            snprintf(g_proc_notify_msg, sizeof(g_proc_notify_msg),
                     "Process '%s' completed (tab %d, %lldm %llds)",
                     comm[0] ? comm : "shell", i + 1, mins, secs);
        } else {
            snprintf(g_proc_notify_msg, sizeof(g_proc_notify_msg),
                     "Process '%s' completed (tab %d, %llds)",
                     comm[0] ? comm : "shell", i + 1, secs);
        }
        g_proc_notify_pending = 1;
        return 1;
    }
    return 0;
}

void nitty_gtk4_proc_notify_get(char *buf, int32_t bufsz)
{
    if (!buf || bufsz <= 0) return;
    strncpy(buf, g_proc_notify_msg, (size_t)(bufsz - 1));
    buf[bufsz - 1] = '\0';
    g_proc_notify_msg[0] = '\0';
}
const char *nitty_gtk4_proc_notify_msg(void)
{
    return g_proc_notify_msg;
}

/* ═══════════════════════════════════════════════════════════════════════
 * v0.8.2: SSH Authentication Dialogs
 * ═══════════════════════════════════════════════════════════════════════ */

static char g_ssh_password_result[512];

/* Show a masked password/passphrase input dialog.
 * title:  dialog window title (e.g. "SSH Password")
 * prompt: label text (e.g. "Password for user@host:")
 * Returns the entered password (static buffer), or "" if cancelled. */
const char *nitty_gtk4_prompt_password(const char *title, const char *prompt)
{
    g_ssh_password_result[0] = '\0';

    PromptState state;
    state.loop      = g_main_loop_new(NULL, FALSE);
    state.confirmed = 0;
    state.result[0] = '\0';

    /* Dialog window */
    GtkWidget *dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), title ? title : "SSH Password");
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(g_main_window));
    gtk_window_set_default_size(GTK_WINDOW(dialog), 380, -1);
    gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);

    /* Layout */
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(box, 16);
    gtk_widget_set_margin_end(box, 16);
    gtk_widget_set_margin_top(box, 16);
    gtk_widget_set_margin_bottom(box, 16);
    gtk_window_set_child(GTK_WINDOW(dialog), box);

    GtkWidget *label = gtk_label_new(prompt ? prompt : "Password:");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), label);

    /* Password entry with visibility=FALSE for masking */
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(entry), FALSE);
    gtk_entry_set_input_purpose(GTK_ENTRY(entry), GTK_INPUT_PURPOSE_PASSWORD);
    gtk_box_append(GTK_BOX(box), entry);
    g_signal_connect(entry, "activate", G_CALLBACK(on_prompt_entry_activate), &state);

    GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(btn_box, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(box), btn_box);

    GtkWidget *cancel_btn = gtk_button_new_with_label("Cancel");
    GtkWidget *ok_btn     = gtk_button_new_with_label("OK");
    gtk_box_append(GTK_BOX(btn_box), cancel_btn);
    gtk_box_append(GTK_BOX(btn_box), ok_btn);

    g_signal_connect(cancel_btn, "clicked", G_CALLBACK(on_prompt_cancel), &state);
    g_signal_connect(ok_btn,     "clicked", G_CALLBACK(on_prompt_ok),     &state);

    gtk_widget_set_visible(dialog, TRUE);
    g_main_loop_run(state.loop);

    if (state.confirmed) {
        const char *text = gtk_editable_get_text(GTK_EDITABLE(entry));
        if (text) {
            strncpy(g_ssh_password_result, text, sizeof(g_ssh_password_result) - 1);
            g_ssh_password_result[sizeof(g_ssh_password_result) - 1] = '\0';
        }
    }

    gtk_window_destroy(GTK_WINDOW(dialog));
    g_main_loop_unref(state.loop);

    return g_ssh_password_result;
}

/* Host key verification dialog state */
typedef struct {
    GMainLoop *loop;
    int        accepted; /* 1 = accept, 0 = reject/cancel */
} HostKeyState;

static void on_hk_accept(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    HostKeyState *state = (HostKeyState *)user_data;
    state->accepted = 1;
    g_main_loop_quit(state->loop);
}

static void on_hk_reject(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    HostKeyState *state = (HostKeyState *)user_data;
    state->accepted = 0;
    g_main_loop_quit(state->loop);
}

/* Show a host key mismatch warning dialog.
 * host:       the server hostname/IP
 * key_type:   "ssh-rsa", "ssh-ed25519", etc.
 * Returns 1 if user accepts (trust anyway), 0 if rejected/cancelled. */
int64_t nitty_gtk4_host_key_dialog(const char *host, const char *key_type)
{
    char msg[768];
    snprintf(msg, sizeof(msg),
             "WARNING: Host key for '%s' has changed!\n\n"
             "Key type: %s\n\n"
             "This could indicate a man-in-the-middle attack.\n"
             "Do you want to continue connecting?",
             host ? host : "(unknown)",
             key_type ? key_type : "(unknown)");

    HostKeyState state;
    state.loop     = g_main_loop_new(NULL, FALSE);
    state.accepted = 0;

    GtkWidget *dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "Host Key Verification Failed");
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(g_main_window));
    gtk_window_set_default_size(GTK_WINDOW(dialog), 460, -1);
    gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(box, 16);
    gtk_widget_set_margin_end(box, 16);
    gtk_widget_set_margin_top(box, 16);
    gtk_widget_set_margin_bottom(box, 16);
    gtk_window_set_child(GTK_WINDOW(dialog), box);

    GtkWidget *label = gtk_label_new(msg);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), label);

    GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(btn_box, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(box), btn_box);

    GtkWidget *reject_btn = gtk_button_new_with_label("Cancel");
    GtkWidget *accept_btn = gtk_button_new_with_label("Connect Anyway");
    gtk_box_append(GTK_BOX(btn_box), reject_btn);
    gtk_box_append(GTK_BOX(btn_box), accept_btn);

    g_signal_connect(reject_btn, "clicked", G_CALLBACK(on_hk_reject), &state);
    g_signal_connect(accept_btn, "clicked", G_CALLBACK(on_hk_accept), &state);

    gtk_widget_set_visible(dialog, TRUE);
    g_main_loop_run(state.loop);

    gtk_window_destroy(GTK_WINDOW(dialog));
    g_main_loop_unref(state.loop);

    return (int64_t)state.accepted;
}


/* ═══════════════════════════════════════════════════════════════════════════ */

/* ═══════════════════════════════════════════════════════════════════════════ */
/* v0.8.3 — Connection Manager UI                                            */
/* ═══════════════════════════════════════════════════════════════════════════ */
/* Note: paned/box/scrolled_window already exist from v0.5.x — reused as-is */

int64_t nitty_gtk4_list_box_new(void) {
    GtkWidget *lb = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(lb), GTK_SELECTION_SINGLE);
    return (int64_t)(uintptr_t)lb;
}

/* Append a row with label text. Returns the row index. */
int64_t nitty_gtk4_list_box_append(int64_t lb, const char *label) {
    GtkWidget *lbox = (GtkWidget *)(uintptr_t)lb;
    GtkWidget *row  = gtk_list_box_row_new();
    GtkWidget *lbl  = gtk_label_new(label);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0f);
    gtk_widget_set_margin_start(lbl, 8);
    gtk_widget_set_margin_end(lbl, 8);
    gtk_widget_set_margin_top(lbl, 4);
    gtk_widget_set_margin_bottom(lbl, 4);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), lbl);
    gtk_list_box_append(GTK_LIST_BOX(lbox), row);
    /* return 0-based index of appended row */
    int idx = 0;
    GtkListBoxRow *r = gtk_list_box_get_row_at_index(GTK_LIST_BOX(lbox), 0);
    while (r != NULL) {
        idx++;
        r = gtk_list_box_get_row_at_index(GTK_LIST_BOX(lbox), idx);
    }
    return (int64_t)(idx - 1);
}

int64_t nitty_gtk4_list_box_get_selected(int64_t lb) {
    GtkListBoxRow *row = gtk_list_box_get_selected_row(
        GTK_LIST_BOX((GtkWidget *)(uintptr_t)lb));
    if (!row) return -1;
    return (int64_t)gtk_list_box_row_get_index(row);
}

void nitty_gtk4_list_box_clear(int64_t lb) {
    GtkWidget *lbox = (GtkWidget *)(uintptr_t)lb;
    GtkListBoxRow *row;
    while ((row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(lbox), 0)) != NULL) {
        gtk_list_box_remove(GTK_LIST_BOX(lbox), GTK_WIDGET(row));
    }
}

void nitty_gtk4_list_box_set_group_header(int64_t lb, int64_t row_idx,
                                           const char *header) {
    GtkListBoxRow *row = gtk_list_box_get_row_at_index(
        GTK_LIST_BOX((GtkWidget *)(uintptr_t)lb), (int)row_idx);
    if (!row) return;
    GtkWidget *hdr = gtk_label_new(header);
    gtk_label_set_xalign(GTK_LABEL(hdr), 0.0f);
    /* Bold the group header */
    PangoAttrList *attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
    gtk_label_set_attributes(GTK_LABEL(hdr), attrs);
    pango_attr_list_unref(attrs);
    gtk_list_box_row_set_header(row, hdr);
}

/* ── GtkEntry ────────────────────────────────────────────────────────────── */

int64_t nitty_gtk4_entry_new(void) {
    GtkWidget *e = gtk_entry_new();
    return (int64_t)(uintptr_t)e;
}

const char *nitty_gtk4_entry_get_text(int64_t entry) {
    GtkEntryBuffer *buf = gtk_entry_get_buffer(
        GTK_ENTRY((GtkWidget *)(uintptr_t)entry));
    return gtk_entry_buffer_get_text(buf);
}

void nitty_gtk4_entry_set_placeholder(int64_t entry, const char *text) {
    gtk_entry_set_placeholder_text(GTK_ENTRY((GtkWidget *)(uintptr_t)entry), text);
}

/* ── GtkButton ───────────────────────────────────────────────────────────── */

int64_t nitty_gtk4_button_new(const char *label) {
    GtkWidget *btn = gtk_button_new_with_label(label);
    return (int64_t)(uintptr_t)btn;
}

void nitty_gtk4_button_set_sensitive(int64_t btn, int32_t sensitive) {
    gtk_widget_set_sensitive((GtkWidget *)(uintptr_t)btn, sensitive ? TRUE : FALSE);
}

/* ── GtkLabel ────────────────────────────────────────────────────────────── */

int64_t nitty_gtk4_label_new(const char *text) {
    GtkWidget *lbl = gtk_label_new(text);
    return (int64_t)(uintptr_t)lbl;
}

void nitty_gtk4_label_set_text(int64_t label, const char *text) {
    gtk_label_set_text(GTK_LABEL((GtkWidget *)(uintptr_t)label), text);
}

/* ── Connection Manager Composite Sidebar ───────────────────────────────── */

/* Event queue — simple ring buffer for Nitpick polling */
#define CM_EVENT_NONE        0
#define CM_EVENT_CONNECT     1
#define CM_EVENT_NEW         2
#define CM_EVENT_EDIT        3
#define CM_EVENT_DELETE      4
#define CM_EVENT_IMPORT      5
#define CM_EVENT_QUICK_CONN  6

static int64_t g_cm_event_buf[16];
static int     g_cm_event_head = 0;
static int     g_cm_event_tail = 0;
static int64_t g_cm_event_profile_id = -1;
static GtkWidget *g_cm_widget   = NULL;
static GtkWidget *g_cm_listbox  = NULL;
static GtkWidget *g_cm_entry    = NULL;
static GtkWidget *g_cm_paned    = NULL;

static void cm_push_event(int64_t code) {
    int next = (g_cm_event_tail + 1) % 16;
    if (next != g_cm_event_head) {
        g_cm_event_buf[g_cm_event_tail] = code;
        g_cm_event_tail = next;
    }
}

static void on_cm_connect_clicked(GtkButton *btn, gpointer data) {
    (void)btn; (void)data;
    cm_push_event(CM_EVENT_CONNECT);
}
static void on_cm_new_clicked(GtkButton *btn, gpointer data) {
    (void)btn; (void)data;
    cm_push_event(CM_EVENT_NEW);
}
static void on_cm_edit_clicked(GtkButton *btn, gpointer data) {
    (void)btn; (void)data;
    cm_push_event(CM_EVENT_EDIT);
}
static void on_cm_delete_clicked(GtkButton *btn, gpointer data) {
    (void)btn; (void)data;
    cm_push_event(CM_EVENT_DELETE);
}
static void on_cm_import_clicked(GtkButton *btn, gpointer data) {
    (void)btn; (void)data;
    cm_push_event(CM_EVENT_IMPORT);
}
static void on_cm_entry_activate(GtkEntry *entry, gpointer data) {
    (void)entry; (void)data;
    cm_push_event(CM_EVENT_QUICK_CONN);
}
static void on_cm_row_activated(GtkListBox *lb, GtkListBoxRow *row, gpointer data) {
    (void)lb; (void)data;
    g_cm_event_profile_id = (int64_t)gtk_list_box_row_get_index(row);
    cm_push_event(CM_EVENT_CONNECT);
}

int64_t nitty_gtk4_cm_create(void) {
    /* Outer vertical box: quick-connect bar + scrolled list + button bar */
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(vbox, 240, -1);

    /* Quick connect label + entry */
    GtkWidget *qc_label = gtk_label_new("Quick Connect");
    gtk_label_set_xalign(GTK_LABEL(qc_label), 0.0f);
    gtk_widget_set_margin_start(qc_label, 8);
    gtk_widget_set_margin_top(qc_label, 8);
    gtk_box_append(GTK_BOX(vbox), qc_label);

    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "user@host:port");
    gtk_widget_set_margin_start(entry, 4);
    gtk_widget_set_margin_end(entry, 4);
    gtk_widget_set_margin_bottom(entry, 4);
    g_signal_connect(entry, "activate", G_CALLBACK(on_cm_entry_activate), NULL);
    gtk_box_append(GTK_BOX(vbox), entry);
    g_cm_entry = entry;

    /* Separator */
    gtk_box_append(GTK_BOX(vbox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    /* Profiles label */
    GtkWidget *pl_label = gtk_label_new("Saved Profiles");
    gtk_label_set_xalign(GTK_LABEL(pl_label), 0.0f);
    gtk_widget_set_margin_start(pl_label, 8);
    gtk_widget_set_margin_top(pl_label, 6);
    gtk_box_append(GTK_BOX(vbox), pl_label);

    /* Scrolled list box */
    GtkWidget *sw = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(sw, TRUE);
    GtkWidget *lb = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(lb), GTK_SELECTION_SINGLE);
    g_signal_connect(lb, "row-activated", G_CALLBACK(on_cm_row_activated), NULL);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), lb);
    gtk_box_append(GTK_BOX(vbox), sw);
    g_cm_listbox = lb;

    /* Button bar */
    gtk_box_append(GTK_BOX(vbox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    GtkWidget *btn_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_set_margin_start(btn_bar, 4);
    gtk_widget_set_margin_end(btn_bar, 4);
    gtk_widget_set_margin_top(btn_bar, 4);
    gtk_widget_set_margin_bottom(btn_bar, 4);

    GtkWidget *btn_connect = gtk_button_new_with_label("Connect");
    GtkWidget *btn_new     = gtk_button_new_with_label("New");
    GtkWidget *btn_edit    = gtk_button_new_with_label("Edit");
    GtkWidget *btn_del     = gtk_button_new_with_label("Delete");
    GtkWidget *btn_import  = gtk_button_new_with_label("Import");

    g_signal_connect(btn_connect, "clicked", G_CALLBACK(on_cm_connect_clicked), NULL);
    g_signal_connect(btn_new,     "clicked", G_CALLBACK(on_cm_new_clicked),     NULL);
    g_signal_connect(btn_edit,    "clicked", G_CALLBACK(on_cm_edit_clicked),    NULL);
    g_signal_connect(btn_del,     "clicked", G_CALLBACK(on_cm_delete_clicked),  NULL);
    g_signal_connect(btn_import,  "clicked", G_CALLBACK(on_cm_import_clicked),  NULL);

    gtk_box_append(GTK_BOX(btn_bar), btn_connect);
    gtk_box_append(GTK_BOX(btn_bar), btn_new);
    gtk_box_append(GTK_BOX(btn_bar), btn_edit);
    gtk_box_append(GTK_BOX(btn_bar), btn_del);
    gtk_box_append(GTK_BOX(btn_bar), btn_import);
    gtk_box_append(GTK_BOX(vbox), btn_bar);

    g_cm_widget = vbox;
    return (int64_t)(uintptr_t)vbox;
}

void nitty_gtk4_cm_set_visible(int64_t cm, int32_t visible) {
    GtkWidget *w = (GtkWidget *)(uintptr_t)cm;
    gtk_widget_set_visible(w, visible ? TRUE : FALSE);
}

int64_t nitty_gtk4_cm_list_box(int64_t cm) {
    (void)cm;
    return (int64_t)(uintptr_t)g_cm_listbox;
}

int64_t nitty_gtk4_cm_entry(int64_t cm) {
    (void)cm;
    return (int64_t)(uintptr_t)g_cm_entry;
}

int64_t nitty_gtk4_cm_event_poll(void) {
    if (g_cm_event_head == g_cm_event_tail) return CM_EVENT_NONE;
    int64_t ev = g_cm_event_buf[g_cm_event_head];
    g_cm_event_head = (g_cm_event_head + 1) % 16;
    return ev;
}

int64_t nitty_gtk4_cm_event_profile_id(void) {
    return g_cm_event_profile_id;
}

/* ── Sidebar Attach ──────────────────────────────────────────────────────── */

void nitty_gtk4_sidebar_attach(int64_t win, int64_t drawing_area, int64_t sidebar) {
    GtkWindow  *window = GTK_WINDOW((GtkWidget *)(uintptr_t)win);
    GtkWidget  *da     = (GtkWidget *)(uintptr_t)drawing_area;
    GtkWidget  *sb     = (GtkWidget *)(uintptr_t)sidebar;

    /* Remove existing child (the DrawingArea) from the window */
    GtkWidget *old_child = gtk_window_get_child(window);
    if (old_child) {
        g_object_ref(da);  /* keep da alive while we re-parent */
        gtk_window_set_child(window, NULL);
    }

    /* Create a horizontal GtkPaned: sidebar | DrawingArea */
    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_set_start_child(GTK_PANED(paned), sb);
    gtk_paned_set_end_child(GTK_PANED(paned), da);
    gtk_paned_set_position(GTK_PANED(paned), 240);
    /* Allow the DrawingArea (end child) to shrink and fill extra space */
    gtk_paned_set_shrink_start_child(GTK_PANED(paned), FALSE);
    gtk_paned_set_resize_start_child(GTK_PANED(paned), FALSE);
    gtk_paned_set_resize_end_child(GTK_PANED(paned), TRUE);

    gtk_window_set_child(window, paned);
    g_cm_paned = paned;

    if (old_child) g_object_unref(da);
}

void nitty_gtk4_sidebar_set_visible(int32_t visible) {
    if (!g_cm_widget) return;
    gtk_widget_set_visible(g_cm_widget, visible ? TRUE : FALSE);
    /* When hiding sidebar, collapse paned to 0 */
    if (g_cm_paned) {
        gtk_paned_set_position(GTK_PANED(g_cm_paned), visible ? 240 : 0);
    }
}

/* ── Profile Editor Dialog ───────────────────────────────────────────────── */

static char g_pe_name[512]     = "";
static char g_pe_group[256]    = "";
static char g_pe_host[512]     = "";
static int64_t g_pe_port       = 22;
static char g_pe_user[256]     = "";
static char g_pe_auth[64]      = "";
static char g_pe_key_path[1024]= "";

typedef struct {
    GMainLoop  *loop;
    int         accepted;
    GtkWidget  *w_name;
    GtkWidget  *w_group;
    GtkWidget  *w_host;
    GtkWidget  *w_port;
    GtkWidget  *w_user;
    GtkWidget  *w_auth;
    GtkWidget  *w_key_path;
} PEState;

static void on_pe_save(GtkButton *btn, gpointer data) {
    (void)btn;
    PEState *s = (PEState *)data;
    s->accepted = 1;
    g_main_loop_quit(s->loop);
}
static void on_pe_cancel(GtkButton *btn, gpointer data) {
    (void)btn;
    PEState *s = (PEState *)data;
    s->accepted = 0;
    g_main_loop_quit(s->loop);
}

int32_t nitty_gtk4_profile_editor_open(const char *name, const char *group,
                                        const char *host, int64_t port,
                                        const char *user, const char *auth_method,
                                        const char *key_path) {
    PEState state = {0};
    state.loop     = g_main_loop_new(NULL, FALSE);
    state.accepted = 0;

    GtkWidget *dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), (name && name[0]) ? "Edit Profile" : "New Profile");
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 420, 340);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(vbox, 16);
    gtk_widget_set_margin_end(vbox, 16);
    gtk_widget_set_margin_top(vbox, 16);
    gtk_widget_set_margin_bottom(vbox, 16);
    gtk_window_set_child(GTK_WINDOW(dialog), vbox);

    /* Helper macro: add label + entry row */
    #define PE_ROW(label_txt, entry_widget, init_val) \
        gtk_box_append(GTK_BOX(vbox), gtk_label_new(label_txt)); \
        entry_widget = gtk_entry_new(); \
        if (init_val && init_val[0]) { \
            gtk_entry_buffer_set_text(gtk_entry_get_buffer(GTK_ENTRY(entry_widget)), \
                                      init_val, -1); \
        } \
        gtk_box_append(GTK_BOX(vbox), entry_widget);

    PE_ROW("Profile Name",  state.w_name,     name);
    PE_ROW("Group",         state.w_group,    group);
    PE_ROW("Host / IP",     state.w_host,     host);

    /* Port */
    gtk_box_append(GTK_BOX(vbox), gtk_label_new("Port"));
    state.w_port = gtk_entry_new();
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%lld", (long long)port);
    gtk_entry_buffer_set_text(gtk_entry_get_buffer(GTK_ENTRY(state.w_port)), port_str, -1);
    gtk_box_append(GTK_BOX(vbox), state.w_port);

    PE_ROW("Username",      state.w_user,     user);
    PE_ROW("Auth Method",   state.w_auth,     auth_method);   /* auto/agent/pubkey/password */
    PE_ROW("Key File Path", state.w_key_path, key_path);

    #undef PE_ROW

    /* Button row */
    GtkWidget *btn_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(btn_row, GTK_ALIGN_END);
    GtkWidget *btn_cancel = gtk_button_new_with_label("Cancel");
    GtkWidget *btn_save   = gtk_button_new_with_label("Save");
    g_signal_connect(btn_cancel, "clicked", G_CALLBACK(on_pe_cancel), &state);
    g_signal_connect(btn_save,   "clicked", G_CALLBACK(on_pe_save),   &state);
    gtk_box_append(GTK_BOX(btn_row), btn_cancel);
    gtk_box_append(GTK_BOX(btn_row), btn_save);
    gtk_box_append(GTK_BOX(vbox), btn_row);

    gtk_widget_set_visible(dialog, TRUE);
    g_main_loop_run(state.loop);

    if (state.accepted) {
        snprintf(g_pe_name,     sizeof(g_pe_name),
                 "%s", gtk_entry_buffer_get_text(gtk_entry_get_buffer(GTK_ENTRY(state.w_name))));
        snprintf(g_pe_group,    sizeof(g_pe_group),
                 "%s", gtk_entry_buffer_get_text(gtk_entry_get_buffer(GTK_ENTRY(state.w_group))));
        snprintf(g_pe_host,     sizeof(g_pe_host),
                 "%s", gtk_entry_buffer_get_text(gtk_entry_get_buffer(GTK_ENTRY(state.w_host))));
        const char *ps = gtk_entry_buffer_get_text(gtk_entry_get_buffer(GTK_ENTRY(state.w_port)));
        g_pe_port = (int64_t)atoll(ps);
        if (g_pe_port <= 0) g_pe_port = 22;
        snprintf(g_pe_user,     sizeof(g_pe_user),
                 "%s", gtk_entry_buffer_get_text(gtk_entry_get_buffer(GTK_ENTRY(state.w_user))));
        snprintf(g_pe_auth,     sizeof(g_pe_auth),
                 "%s", gtk_entry_buffer_get_text(gtk_entry_get_buffer(GTK_ENTRY(state.w_auth))));
        snprintf(g_pe_key_path, sizeof(g_pe_key_path),
                 "%s", gtk_entry_buffer_get_text(gtk_entry_get_buffer(GTK_ENTRY(state.w_key_path))));
    }

    gtk_window_destroy(GTK_WINDOW(dialog));
    g_main_loop_unref(state.loop);
    return (int32_t)state.accepted;
}

const char *nitty_gtk4_profile_editor_get_name(void)     { return g_pe_name; }
const char *nitty_gtk4_profile_editor_get_group(void)    { return g_pe_group; }
const char *nitty_gtk4_profile_editor_get_host(void)     { return g_pe_host; }
int64_t     nitty_gtk4_profile_editor_get_port(void)     { return g_pe_port; }
const char *nitty_gtk4_profile_editor_get_user(void)     { return g_pe_user; }
const char *nitty_gtk4_profile_editor_get_auth(void)     { return g_pe_auth; }
const char *nitty_gtk4_profile_editor_get_key_path(void) { return g_pe_key_path; }

/* ═══════════════════════════════════════════════════════════════════════════ */
/* v0.8.5 — SFTP Browser Panel                                               */
/* ═══════════════════════════════════════════════════════════════════════════ */

/* Event codes (must match constants in nitty_gtk4_shim.h) */
#define SFTP_EV_NONE       0
#define SFTP_EV_ACTIVATE   1   /* entry double-clicked / Enter */
#define SFTP_EV_REFRESH    2
#define SFTP_EV_DOWNLOAD   3
#define SFTP_EV_UPLOAD     4
#define SFTP_EV_RENAME     5
#define SFTP_EV_DELETE     6
#define SFTP_EV_MKDIR      7
#define SFTP_EV_UP         8   /* navigate to parent directory */

/* Per-panel state */
typedef struct {
    GtkWidget *vbox;          /* outer container returned to Nitpick */
    GtkWidget *path_label;    /* current path display */
    GtkWidget *list_box;      /* GtkListBox for file entries */
    GtkWidget *status_label;  /* "N items" / error text */
    GtkWidget *xfer_label;    /* "↓ file.txt 42%" */
    GtkWidget *scroll;        /* GtkScrolledWindow around list_box */
    /* event ring */
    int64_t ev_buf[32];
    int     ev_head;
    int     ev_tail;
    int64_t ev_last_row;      /* row index of last activate event */
} SftpPanel;

/* Only one panel exists at a time (single-window app) */
static SftpPanel g_sftp;
static int       g_sftp_initialized = 0;

/* Right-hand GtkPaned (SFTP panel on the right of the terminal) */
static GtkWidget *g_sftp_paned     = NULL;
static int        g_sftp_visible   = 0;
static int        g_sftp_paned_pos = 280;   /* default panel width */

/* ── Helpers ──────────────────────────────────────────────────────────────── */

static void sftp_push_event(SftpPanel *p, int64_t code, int64_t row)
{
    int next = (p->ev_tail + 1) % 32;
    if (next != p->ev_head) {
        p->ev_buf[p->ev_tail] = code;
        p->ev_tail = next;
        p->ev_last_row = row;
    }
}

/* Context menu callbacks */
static void on_sftp_menu_download(GtkWidget *w, gpointer data)
{
    (void)w;
    SftpPanel *p = (SftpPanel *)data;
    int64_t sel = -1;
    GtkListBoxRow *row = gtk_list_box_get_selected_row(GTK_LIST_BOX(p->list_box));
    if (row) sel = (int64_t)gtk_list_box_row_get_index(row);
    sftp_push_event(p, SFTP_EV_DOWNLOAD, sel);
}
static void on_sftp_menu_upload(GtkWidget *w, gpointer data)
{
    (void)w;
    SftpPanel *p = (SftpPanel *)data;
    sftp_push_event(p, SFTP_EV_UPLOAD, -1);
}
static void on_sftp_menu_rename(GtkWidget *w, gpointer data)
{
    (void)w;
    SftpPanel *p = (SftpPanel *)data;
    int64_t sel = -1;
    GtkListBoxRow *row = gtk_list_box_get_selected_row(GTK_LIST_BOX(p->list_box));
    if (row) sel = (int64_t)gtk_list_box_row_get_index(row);
    sftp_push_event(p, SFTP_EV_RENAME, sel);
}
static void on_sftp_menu_delete(GtkWidget *w, gpointer data)
{
    (void)w;
    SftpPanel *p = (SftpPanel *)data;
    int64_t sel = -1;
    GtkListBoxRow *row = gtk_list_box_get_selected_row(GTK_LIST_BOX(p->list_box));
    if (row) sel = (int64_t)gtk_list_box_row_get_index(row);
    sftp_push_event(p, SFTP_EV_DELETE, sel);
}
static void on_sftp_menu_mkdir(GtkWidget *w, gpointer data)
{
    (void)w;
    SftpPanel *p = (SftpPanel *)data;
    sftp_push_event(p, SFTP_EV_MKDIR, -1);
}

static void on_sftp_row_activated(GtkListBox *lb, GtkListBoxRow *row, gpointer data)
{
    (void)lb;
    SftpPanel *p = (SftpPanel *)data;
    int64_t idx = row ? (int64_t)gtk_list_box_row_get_index(row) : -1;
    sftp_push_event(p, SFTP_EV_ACTIVATE, idx);
}

/* Right-click → show context menu */
static void on_sftp_list_right_click(GtkGestureClick *gesture, int n_press,
                                      double x, double y, gpointer data)
{
    (void)n_press; (void)x; (void)y;
    SftpPanel *p = (SftpPanel *)data;

    GtkWidget *menu = gtk_popover_menu_new_from_model(NULL);

    GMenu *gm = g_menu_new();
    GMenuItem *dl  = g_menu_item_new("Download",   NULL);
    GMenuItem *ul  = g_menu_item_new("Upload here", NULL);
    GMenuItem *ren = g_menu_item_new("Rename",      NULL);
    GMenuItem *del = g_menu_item_new("Delete",      NULL);
    GMenuItem *mkd = g_menu_item_new("New Folder",  NULL);
    g_menu_append_item(gm, dl);
    g_menu_append_item(gm, ul);
    g_menu_append_item(gm, ren);
    g_menu_append_item(gm, del);
    g_menu_append_item(gm, mkd);
    g_object_unref(dl); g_object_unref(ul); g_object_unref(ren);
    g_object_unref(del); g_object_unref(mkd);

    /* Use a simple GtkPopover with manual buttons instead of GMenuModel
     * (avoids needing GActions wired up) */
    g_object_unref(gm);
    gtk_widget_unparent(menu);

    /* Build a simple popover with buttons */
    GtkWidget *pop = gtk_popover_new();
    gtk_widget_set_parent(pop, p->list_box);
    GdkRectangle rect = { (int)x, (int)y, 1, 1 };
    gtk_popover_set_pointing_to(GTK_POPOVER(pop), &rect);
    gtk_popover_set_has_arrow(GTK_POPOVER(pop), FALSE);

    GtkWidget *btnbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_margin_start(btnbox, 4);
    gtk_widget_set_margin_end(btnbox, 4);
    gtk_widget_set_margin_top(btnbox, 4);
    gtk_widget_set_margin_bottom(btnbox, 4);

    struct { const char *label; GCallback cb; } items[] = {
        { "Download",    G_CALLBACK(on_sftp_menu_download) },
        { "Upload here", G_CALLBACK(on_sftp_menu_upload)   },
        { "Rename",      G_CALLBACK(on_sftp_menu_rename)   },
        { "Delete",      G_CALLBACK(on_sftp_menu_delete)   },
        { "New Folder",  G_CALLBACK(on_sftp_menu_mkdir)    },
    };
    for (int i = 0; i < 5; i++) {
        GtkWidget *b = gtk_button_new_with_label(items[i].label);
        gtk_button_set_has_frame(GTK_BUTTON(b), FALSE);
        g_signal_connect(b, "clicked", items[i].cb, p);
        /* Dismiss popover after click */
        g_signal_connect_swapped(b, "clicked", G_CALLBACK(gtk_popover_popdown), pop);
        gtk_box_append(GTK_BOX(btnbox), b);
    }

    gtk_popover_set_child(GTK_POPOVER(pop), btnbox);
    gtk_popover_popup(GTK_POPOVER(pop));

    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
}

static void on_sftp_up_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    SftpPanel *p = (SftpPanel *)data;
    sftp_push_event(p, SFTP_EV_UP, -1);
}
static void on_sftp_refresh_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    SftpPanel *p = (SftpPanel *)data;
    sftp_push_event(p, SFTP_EV_REFRESH, -1);
}

/* ── Public API ───────────────────────────────────────────────────────────── */

int64_t nitty_gtk4_sftp_create(void)
{
    SftpPanel *p = &g_sftp;
    memset(p, 0, sizeof(*p));
    g_sftp_initialized = 1;

    /* Outer vertical box */
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(vbox, g_sftp_paned_pos, -1);

    /* ── Header: path label + Up/Refresh buttons ── */
    GtkWidget *hdr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_set_margin_start(hdr, 4);
    gtk_widget_set_margin_end(hdr, 4);
    gtk_widget_set_margin_top(hdr, 4);
    gtk_widget_set_margin_bottom(hdr, 2);

    GtkWidget *path_lbl = gtk_label_new("/");
    gtk_label_set_ellipsize(GTK_LABEL(path_lbl), PANGO_ELLIPSIZE_START);
    gtk_label_set_xalign(GTK_LABEL(path_lbl), 0.0f);
    gtk_widget_set_hexpand(path_lbl, TRUE);
    p->path_label = path_lbl;

    GtkWidget *btn_up  = gtk_button_new_with_label("↑");
    GtkWidget *btn_ref = gtk_button_new_with_label("⟳");
    gtk_widget_set_tooltip_text(btn_up,  "Go up");
    gtk_widget_set_tooltip_text(btn_ref, "Refresh");
    g_signal_connect(btn_up,  "clicked", G_CALLBACK(on_sftp_up_clicked),      p);
    g_signal_connect(btn_ref, "clicked", G_CALLBACK(on_sftp_refresh_clicked),  p);

    gtk_box_append(GTK_BOX(hdr), path_lbl);
    gtk_box_append(GTK_BOX(hdr), btn_up);
    gtk_box_append(GTK_BOX(hdr), btn_ref);
    gtk_box_append(GTK_BOX(vbox), hdr);
    gtk_box_append(GTK_BOX(vbox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    /* ── File list ── */
    GtkWidget *sw = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(sw, TRUE);
    p->scroll = sw;

    GtkWidget *lb = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(lb), GTK_SELECTION_SINGLE);
    g_signal_connect(lb, "row-activated", G_CALLBACK(on_sftp_row_activated), p);
    p->list_box = lb;

    /* Right-click gesture on the list box */
    GtkGesture *rclick = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(rclick), 3); /* right button */
    g_signal_connect(rclick, "pressed", G_CALLBACK(on_sftp_list_right_click), p);
    gtk_widget_add_controller(lb, GTK_EVENT_CONTROLLER(rclick));

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), lb);
    gtk_box_append(GTK_BOX(vbox), sw);

    /* ── Footer: status label + xfer label ── */
    gtk_box_append(GTK_BOX(vbox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    GtkWidget *footer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_start(footer, 4);
    gtk_widget_set_margin_end(footer, 4);
    gtk_widget_set_margin_top(footer, 2);
    gtk_widget_set_margin_bottom(footer, 4);

    GtkWidget *status_lbl = gtk_label_new("Connecting…");
    gtk_label_set_xalign(GTK_LABEL(status_lbl), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(status_lbl), PANGO_ELLIPSIZE_END);
    p->status_label = status_lbl;

    GtkWidget *xfer_lbl = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(xfer_lbl), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(xfer_lbl), PANGO_ELLIPSIZE_END);
    p->xfer_label = xfer_lbl;

    gtk_box_append(GTK_BOX(footer), status_lbl);
    gtk_box_append(GTK_BOX(footer), xfer_lbl);
    gtk_box_append(GTK_BOX(vbox), footer);

    p->vbox = vbox;
    return (int64_t)(uintptr_t)vbox;
}

/* Attach the SFTP panel on the RIGHT side of the terminal drawing area.
 * If sidebar_attach already created a GtkPaned (left sidebar), we nest
 * another GtkPaned inside the end-child of that one.
 * If no sidebar exists yet, we wrap the drawing area directly. */
void nitty_gtk4_sftp_panel_attach(int64_t win, int64_t drawing_area,
                                   int64_t sftp_panel)
{
    GtkWindow *window  = GTK_WINDOW((GtkWidget *)(uintptr_t)win);
    GtkWidget *da      = (GtkWidget *)(uintptr_t)drawing_area;
    GtkWidget *panel   = (GtkWidget *)(uintptr_t)sftp_panel;

    /* Find the widget that currently contains the drawing area:
     * either the window's direct child or the end-child of a GtkPaned
     * (created by sidebar_attach). */
    GtkWidget *root_child = gtk_window_get_child(window);

    if (root_child && GTK_IS_PANED(root_child)) {
        /* Sidebar paned exists — insert a second paned inside its end-child */
        GtkWidget *existing_end = gtk_paned_get_end_child(GTK_PANED(root_child));
        if (existing_end == da) {
            g_object_ref(da);
            gtk_paned_set_end_child(GTK_PANED(root_child), NULL);

            GtkWidget *paned2 = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
            gtk_paned_set_start_child(GTK_PANED(paned2), da);
            gtk_paned_set_end_child(GTK_PANED(paned2), panel);
            gtk_paned_set_position(GTK_PANED(paned2), 10000); /* start maximised */
            gtk_paned_set_shrink_start_child(GTK_PANED(paned2), FALSE);
            gtk_paned_set_resize_start_child(GTK_PANED(paned2), TRUE);
            gtk_paned_set_resize_end_child(GTK_PANED(paned2), FALSE);

            gtk_paned_set_end_child(GTK_PANED(root_child), paned2);
            g_sftp_paned = paned2;
            g_object_unref(da);
            return;
        }
    }

    /* No sidebar — wrap drawing area directly */
    if (root_child == da) {
        g_object_ref(da);
        gtk_window_set_child(window, NULL);
    }

    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_set_start_child(GTK_PANED(paned), da);
    gtk_paned_set_end_child(GTK_PANED(paned), panel);
    gtk_paned_set_position(GTK_PANED(paned), 10000);
    gtk_paned_set_shrink_start_child(GTK_PANED(paned), FALSE);
    gtk_paned_set_resize_start_child(GTK_PANED(paned), TRUE);
    gtk_paned_set_resize_end_child(GTK_PANED(paned), FALSE);

    gtk_window_set_child(window, paned);
    g_sftp_paned = paned;

    if (root_child == da) g_object_unref(da);
}

void nitty_gtk4_sftp_panel_set_visible(int32_t visible)
{
    if (!g_sftp_initialized) return;
    gtk_widget_set_visible(g_sftp.vbox, visible ? TRUE : FALSE);
    g_sftp_visible = visible;
    if (g_sftp_paned) {
        if (visible) {
            /* Restore panel: shrink terminal by panel width */
            int total = gtk_widget_get_width(g_sftp_paned);
            int pos   = (total > g_sftp_paned_pos + 80) ? (total - g_sftp_paned_pos) : total - 240;
            gtk_paned_set_position(GTK_PANED(g_sftp_paned), pos > 0 ? pos : total);
        } else {
            /* Hide: push paned position to full width (end-child collapses) */
            gtk_paned_set_position(GTK_PANED(g_sftp_paned), 100000);
        }
    }
}

/* Sub-widget accessors */
int64_t nitty_gtk4_sftp_path_label(int64_t sftp_widget)
{
    (void)sftp_widget;
    return g_sftp_initialized ? (int64_t)(uintptr_t)g_sftp.path_label : 0;
}
int64_t nitty_gtk4_sftp_list_box(int64_t sftp_widget)
{
    (void)sftp_widget;
    return g_sftp_initialized ? (int64_t)(uintptr_t)g_sftp.list_box : 0;
}
int64_t nitty_gtk4_sftp_status_label(int64_t sftp_widget)
{
    (void)sftp_widget;
    return g_sftp_initialized ? (int64_t)(uintptr_t)g_sftp.status_label : 0;
}
int64_t nitty_gtk4_sftp_xfer_label(int64_t sftp_widget)
{
    (void)sftp_widget;
    return g_sftp_initialized ? (int64_t)(uintptr_t)g_sftp.xfer_label : 0;
}

/* Event poll — returns next event code (0 = none) */
int64_t nitty_gtk4_sftp_event_poll(int64_t sftp_widget)
{
    (void)sftp_widget;
    if (!g_sftp_initialized) return SFTP_EV_NONE;
    SftpPanel *p = &g_sftp;
    if (p->ev_head == p->ev_tail) return SFTP_EV_NONE;
    int64_t ev = p->ev_buf[p->ev_head];
    p->ev_head = (p->ev_head + 1) % 32;
    return ev;
}

/* Row index of the last activate / context-menu event */
int64_t nitty_gtk4_sftp_event_row(void)
{
    return g_sftp_initialized ? g_sftp.ev_last_row : -1;
}
