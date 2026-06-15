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
    if      (strcmp(name, "rename")       == 0) g_ctx_pending_action = 1;
    else if (strcmp(name, "duplicate")    == 0) g_ctx_pending_action = 2;
    else if (strcmp(name, "close")        == 0) g_ctx_pending_action = 3;
    else if (strcmp(name, "close-others") == 0) g_ctx_pending_action = 4;
    else if (strcmp(name, "close-right")  == 0) g_ctx_pending_action = 5;
    else if (strcmp(name, "color-red")    == 0) g_ctx_pending_action = 6;
    else if (strcmp(name, "color-orange") == 0) g_ctx_pending_action = 7;
    else if (strcmp(name, "color-yellow") == 0) g_ctx_pending_action = 8;
    else if (strcmp(name, "color-green")  == 0) g_ctx_pending_action = 9;
    else if (strcmp(name, "color-blue")   == 0) g_ctx_pending_action = 10;
    else if (strcmp(name, "color-purple") == 0) g_ctx_pending_action = 11;
    else if (strcmp(name, "color-pink")   == 0) g_ctx_pending_action = 12;
    else if (strcmp(name, "color-none")   == 0) g_ctx_pending_action = 13;
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
        "color-blue", "color-purple", "color-pink", "color-none", NULL
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
