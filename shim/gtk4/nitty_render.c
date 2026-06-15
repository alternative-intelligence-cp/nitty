/*
 * nitty_render.c — Terminal grid renderer for Nitty
 *
 * v0.0.2: C-side rendering of a monospace character grid using Cairo + Pango.
 *
 * Architecture:
 *   - Nitpick sets font, colors, and grid content via nitty_render_*() calls
 *   - The GTK4 draw callback (on_draw_func) calls nitty_render_frame()
 *   - nitty_render_frame() uses stored state to paint the grid
 *   - A single PangoLayout is reused per frame for performance
 *   - Font metrics (cell width/height) are computed once per font change
 *
 * Coordinates:
 *   - All grid positions are in logical pixels (GTK handles HiDPI scaling)
 *   - Colors use fixed-point × 1000: 0=0.0, 500=0.5, 1000=1.0
 */

#include "nitty_render.h"
#include <gtk/gtk.h>
#include <pango/pangocairo.h>
#include <string.h>
#include <stdlib.h>

/* ── Grid cell structure ──────────────────────────────────────────────── */

typedef struct {
    char ch[8];       /* UTF-8 character (up to 4 bytes + NUL) */
    int  has_fg;      /* Non-zero if per-cell foreground is set */
    int  has_bg;      /* Non-zero if per-cell background is set */
    int  fg_r, fg_g, fg_b;  /* Per-cell foreground (fixed-point) */
    int  bg_r, bg_g, bg_b;  /* Per-cell background (fixed-point) */
} GridCell;

/* ── Render state ─────────────────────────────────────────────────────── */

static char     g_font_desc_str[128]  = "Monospace 12";
static int      g_font_changed        = 1; /* Force initial measurement */

/* Default colors (fixed-point × 1000) */
static int      g_bg_r = 0, g_bg_g = 0, g_bg_b = 0;       /* Black */
static int      g_fg_r = 1000, g_fg_g = 1000, g_fg_b = 1000; /* White */

/* Grid cells */
static GridCell g_grid[NITTY_MAX_ROWS][NITTY_MAX_COLS];
static int      g_grid_cols = 0;
static int      g_grid_rows = 0;

/* Computed cell metrics (pixels) */
static int      g_cell_width    = 0;
static int      g_cell_height   = 0;
static int      g_font_baseline = 0;  /* Pixel offset from cell top to text baseline */

/* Cached Pango font description */
static PangoFontDescription *g_cached_font = NULL;

/* ── Helper: measure font metrics ─────────────────────────────────────── */

static void measure_font(cairo_t *cr)
{
    if (!g_font_changed) return;

    /* Free old cached font */
    if (g_cached_font != NULL) {
        pango_font_description_free(g_cached_font);
    }
    g_cached_font = pango_font_description_from_string(g_font_desc_str);
    if (g_cached_font == NULL) return;

    /*
     * Measure cell dimensions using a reference character.
     * We use PangoLayout to measure "M" — this gives us the exact
     * pixel size of one cell in the monospace grid.
     * This is more reliable than PangoFontMetrics for our use case.
     */
    PangoLayout *layout = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(layout, g_cached_font);
    pango_layout_set_text(layout, "M", 1);

    int char_w = 0, char_h = 0;
    pango_layout_get_pixel_size(layout, &char_w, &char_h);

    /* Compute baseline from ink extents.
     * PangoRectangle y is the ink top relative to the layout origin (negative = above).
     * For text: baseline ≈ -ink_extents.y  (distance from layout top to text top).
     * We use the layout's ink rect to find where the ink starts vertically.
     */
    PangoRectangle ink_rect;
    pango_layout_get_pixel_extents(layout, &ink_rect, NULL);
    g_object_unref(layout);

    if (char_w > 0 && char_h > 0) {
        g_cell_width    = char_w;
        g_cell_height   = char_h;
        /* ink_rect.y is typically 0 or slightly negative; baseline offset is the
         * vertical distance from cell top to where text ink starts. For Pango
         * layouts, ink_rect.y gives the top of the ink relative to the layout
         * origin. A reasonable baseline is simply 0 (Pango places layout at origin).
         * Store as fixed-point × 1000 for Nitpick. */
        g_font_baseline = (ink_rect.y < 0) ? (-ink_rect.y * 1000) : 0;
    }

    g_font_changed = 0;
}

/* ── Configuration functions ──────────────────────────────────────────── */

void nitty_render_set_font(const char *font_desc)
{
    if (font_desc == NULL) return;
    strncpy(g_font_desc_str, font_desc, sizeof(g_font_desc_str) - 1);
    g_font_desc_str[sizeof(g_font_desc_str) - 1] = '\0';
    g_font_changed = 1;
}

void nitty_render_set_bg(int64_t r, int64_t g, int64_t b)
{
    g_bg_r = (int)r;
    g_bg_g = (int)g;
    g_bg_b = (int)b;
}

void nitty_render_set_fg(int64_t r, int64_t g, int64_t b)
{
    g_fg_r = (int)r;
    g_fg_g = (int)g;
    g_fg_b = (int)b;
}

void nitty_render_set_cell(int64_t col, int64_t row, const char *ch)
{
    if (col < 0 || col >= NITTY_MAX_COLS || row < 0 || row >= NITTY_MAX_ROWS) return;
    if (ch == NULL) return;

    strncpy(g_grid[(int)row][(int)col].ch, ch, sizeof(g_grid[0][0].ch) - 1);
    g_grid[(int)row][(int)col].ch[sizeof(g_grid[0][0].ch) - 1] = '\0';

    /* Track grid bounds */
    if ((int)col + 1 > g_grid_cols) g_grid_cols = (int)col + 1;
    if ((int)row + 1 > g_grid_rows) g_grid_rows = (int)row + 1;
}

void nitty_render_set_cell_fg(int64_t col, int64_t row,
                               int64_t r, int64_t g, int64_t b)
{
    if (col < 0 || col >= NITTY_MAX_COLS || row < 0 || row >= NITTY_MAX_ROWS) return;
    GridCell *cell = &g_grid[(int)row][(int)col];
    cell->has_fg = 1;
    cell->fg_r = (int)r;
    cell->fg_g = (int)g;
    cell->fg_b = (int)b;
}

void nitty_render_set_cell_bg(int64_t col, int64_t row,
                               int64_t r, int64_t g, int64_t b)
{
    if (col < 0 || col >= NITTY_MAX_COLS || row < 0 || row >= NITTY_MAX_ROWS) return;
    GridCell *cell = &g_grid[(int)row][(int)col];
    cell->has_bg = 1;
    cell->bg_r = (int)r;
    cell->bg_g = (int)g;
    cell->bg_b = (int)b;
}

void nitty_render_fill_text(const char *text, int64_t cols, int64_t rows)
{
    if (text == NULL || cols <= 0 || rows <= 0) return;

    int text_len = (int)strlen(text);
    if (text_len == 0) return;

    int max_cols = (int)cols;
    int max_rows = (int)rows;
    if (max_cols > NITTY_MAX_COLS) max_cols = NITTY_MAX_COLS;
    if (max_rows > NITTY_MAX_ROWS) max_rows = NITTY_MAX_ROWS;

    /* Clear existing grid */
    memset(g_grid, 0, sizeof(g_grid));

    int text_idx = 0;
    for (int r = 0; r < max_rows; r++) {
        for (int c = 0; c < max_cols; c++) {
            char buf[2] = { text[text_idx % text_len], '\0' };
            strncpy(g_grid[r][c].ch, buf, sizeof(g_grid[r][c].ch) - 1);
            text_idx++;
        }
    }

    g_grid_cols = max_cols;
    g_grid_rows = max_rows;
}

int64_t nitty_render_get_cell_width(void)
{
    return (int64_t)g_cell_width;
}

int64_t nitty_render_get_cell_height(void)
{
    return (int64_t)g_cell_height;
}

int64_t nitty_render_get_font_baseline(void)
{
    return (int64_t)g_font_baseline;
}

void nitty_render_clear_grid(void)
{
    memset(g_grid, 0, sizeof(g_grid));
    g_grid_cols = 0;
    g_grid_rows = 0;
}

/* ── Render the grid ──────────────────────────────────────────────────── */

void nitty_render_frame(void *cr_ptr, int width, int height)
{
    cairo_t *cr = (cairo_t *)cr_ptr;
    if (cr == NULL) return;

    /* Measure font if needed */
    measure_font(cr);
    if (g_cell_width <= 0 || g_cell_height <= 0) return;

    /* 1. Fill entire background */
    cairo_set_source_rgb(cr,
        (double)g_bg_r / 1000.0,
        (double)g_bg_g / 1000.0,
        (double)g_bg_b / 1000.0
    );
    cairo_paint(cr);

    /* Calculate visible grid dimensions */
    int visible_cols = width / g_cell_width;
    int visible_rows = height / g_cell_height;
    if (visible_cols > g_grid_cols) visible_cols = g_grid_cols;
    if (visible_rows > g_grid_rows) visible_rows = g_grid_rows;

    if (visible_cols <= 0 || visible_rows <= 0) return;

    /* Create a reusable PangoLayout for the frame */
    PangoLayout *layout = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(layout, g_cached_font);

    /* 2. Draw cell backgrounds (only cells with custom bg) */
    for (int r = 0; r < visible_rows; r++) {
        for (int c = 0; c < visible_cols; c++) {
            GridCell *cell = &g_grid[r][c];
            if (cell->has_bg) {
                cairo_set_source_rgb(cr,
                    (double)cell->bg_r / 1000.0,
                    (double)cell->bg_g / 1000.0,
                    (double)cell->bg_b / 1000.0
                );
                cairo_rectangle(cr,
                    (double)(c * g_cell_width),
                    (double)(r * g_cell_height),
                    (double)g_cell_width,
                    (double)g_cell_height
                );
                cairo_fill(cr);
            }
        }
    }

    /* 3. Draw text characters */
    for (int r = 0; r < visible_rows; r++) {
        for (int c = 0; c < visible_cols; c++) {
            GridCell *cell = &g_grid[r][c];

            /* Skip empty cells */
            if (cell->ch[0] == '\0' || cell->ch[0] == ' ') continue;

            /* Set text color */
            if (cell->has_fg) {
                cairo_set_source_rgb(cr,
                    (double)cell->fg_r / 1000.0,
                    (double)cell->fg_g / 1000.0,
                    (double)cell->fg_b / 1000.0
                );
            } else {
                cairo_set_source_rgb(cr,
                    (double)g_fg_r / 1000.0,
                    (double)g_fg_g / 1000.0,
                    (double)g_fg_b / 1000.0
                );
            }

            /* Position and render */
            cairo_move_to(cr,
                (double)(c * g_cell_width),
                (double)(r * g_cell_height)
            );
            pango_layout_set_text(layout, cell->ch, -1);
            pango_cairo_show_layout(cr, layout);
        }
    }

    g_object_unref(layout);
}
