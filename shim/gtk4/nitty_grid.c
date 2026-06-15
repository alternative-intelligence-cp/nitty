/*
 * nitty_grid.c — Terminal grid implementation for Nitty
 *
 * v0.0.4: Cell buffer, cursor, resize, integrated rendering.
 *
 * Cell layout (10 bytes):
 *   [0..3]  character (int32_t)
 *   [4..6]  foreground R, G, B (uint8_t each)
 *   [7..9]  background R, G, B (uint8_t each)
 *
 * Buffer: flat, row-major. Index = (row * cols + col) * CELL_SIZE.
 * Font metrics computed via Pango (same as nitty_render.c but independent).
 */

#include "nitty_grid.h"
#include <gtk/gtk.h>
#include <pango/pangocairo.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ── Constants ────────────────────────────────────────────────────────── */

#define CELL_SIZE 10

/* ── Grid state ───────────────────────────────────────────────────────── */

static uint8_t *g_cells       = NULL;
static int64_t  g_cols        = 0;
static int64_t  g_rows        = 0;
static int64_t  g_cell_w      = 0;
static int64_t  g_cell_h      = 0;

/* Font description string (stored for resize) */
static char     g_font_desc[256] = "Monospace 12";

/* Cursor */
static int64_t  g_cursor_col     = 0;
static int64_t  g_cursor_row     = 0;
static int64_t  g_cursor_style   = 0;  /* 0=block, 1=underline, 2=bar */
static int64_t  g_cursor_visible = 1;

/* Default colors for cleared cells */
static uint8_t  g_default_fg[3] = {204, 204, 204};  /* light gray */
static uint8_t  g_default_bg[3] = {30,  30,  30};   /* dark background */

/* ── Helpers ──────────────────────────────────────────────────────────── */

static inline uint8_t *cell_ptr(int64_t col, int64_t row)
{
    if (col < 0 || col >= g_cols || row < 0 || row >= g_rows)
        return NULL;
    return &g_cells[(row * g_cols + col) * CELL_SIZE];
}

/**
 * Compute cell width and height using Pango font metrics.
 * Returns 0 on success, -1 on failure.
 */
static int compute_metrics(const char *font_desc, int64_t *out_w, int64_t *out_h)
{
    /* Create a temporary surface for measurement */
    cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    if (surf == NULL) return -1;

    cairo_t *cr = cairo_create(surf);
    if (cr == NULL) { cairo_surface_destroy(surf); return -1; }

    PangoLayout *layout = pango_cairo_create_layout(cr);
    PangoFontDescription *fd = pango_font_description_from_string(font_desc);
    pango_layout_set_font_description(layout, fd);

    /* Measure reference character 'M' for monospace metrics */
    pango_layout_set_text(layout, "M", 1);
    int pw = 0, ph = 0;
    pango_layout_get_pixel_size(layout, &pw, &ph);

    if (pw <= 0) pw = 8;
    if (ph <= 0) ph = 16;

    *out_w = (int64_t)pw;
    *out_h = (int64_t)ph;

    pango_font_description_free(fd);
    g_object_unref(layout);
    cairo_destroy(cr);
    cairo_surface_destroy(surf);

    return 0;
}

/* ── Grid lifecycle ───────────────────────────────────────────────────── */

int64_t nitty_grid_init(int64_t width_px, int64_t height_px, const char *font_desc)
{
    if (g_cells != NULL) {
        free(g_cells);
        g_cells = NULL;
    }

    /* Store font description for later resize */
    if (font_desc != NULL) {
        strncpy(g_font_desc, font_desc, sizeof(g_font_desc) - 1);
        g_font_desc[sizeof(g_font_desc) - 1] = '\0';
    }

    /* Compute font metrics */
    if (compute_metrics(g_font_desc, &g_cell_w, &g_cell_h) != 0) {
        fprintf(stderr, "nitty_grid_init: failed to compute font metrics\n");
        return 0;
    }

    g_cols = width_px / g_cell_w;
    g_rows = height_px / g_cell_h;
    if (g_cols < 1) g_cols = 1;
    if (g_rows < 1) g_rows = 1;

    /* Allocate cell buffer */
    size_t buf_size = (size_t)(g_cols * g_rows) * CELL_SIZE;
    g_cells = (uint8_t *)malloc(buf_size);
    if (g_cells == NULL) {
        fprintf(stderr, "nitty_grid_init: malloc(%zu) failed\n", buf_size);
        return 0;
    }

    /* Clear to spaces with default colors */
    nitty_grid_clear(
        (int64_t)((g_default_fg[0] << 16) | (g_default_fg[1] << 8) | g_default_fg[2]),
        (int64_t)((g_default_bg[0] << 16) | (g_default_bg[1] << 8) | g_default_bg[2])
    );

    /* Reset cursor */
    g_cursor_col     = 0;
    g_cursor_row     = 0;
    g_cursor_visible = 1;

    fprintf(stdout, "Grid: %ldx%ld cells (%ldx%ld px each) in %ldx%ld window\n",
            (long)g_cols, (long)g_rows,
            (long)g_cell_w, (long)g_cell_h,
            (long)width_px, (long)height_px);
    fflush(stdout);

    return 1;
}

void nitty_grid_destroy(void)
{
    if (g_cells != NULL) {
        free(g_cells);
        g_cells = NULL;
    }
    g_cols = 0;
    g_rows = 0;
}

/* ── Grid dimensions ──────────────────────────────────────────────────── */

int64_t nitty_grid_get_cols(void)        { return g_cols; }
int64_t nitty_grid_get_rows(void)        { return g_rows; }
int64_t nitty_grid_get_cell_width(void)  { return g_cell_w; }
int64_t nitty_grid_get_cell_height(void) { return g_cell_h; }

/* ── Cell access ──────────────────────────────────────────────────────── */

void nitty_grid_set_cell(int64_t col, int64_t row, int64_t ch, int64_t fg, int64_t bg)
{
    uint8_t *c = cell_ptr(col, row);
    if (c == NULL) return;

    /* Character (4 bytes, little-endian) */
    int32_t ch32 = (int32_t)ch;
    memcpy(c, &ch32, 4);

    /* FG (3 bytes from packed 0xRRGGBB) */
    c[4] = (uint8_t)((fg >> 16) & 0xFF);
    c[5] = (uint8_t)((fg >> 8)  & 0xFF);
    c[6] = (uint8_t)( fg        & 0xFF);

    /* BG (3 bytes from packed 0xRRGGBB) */
    c[7] = (uint8_t)((bg >> 16) & 0xFF);
    c[8] = (uint8_t)((bg >> 8)  & 0xFF);
    c[9] = (uint8_t)( bg        & 0xFF);
}

int64_t nitty_grid_get_cell_char(int64_t col, int64_t row)
{
    uint8_t *c = cell_ptr(col, row);
    if (c == NULL) return 0x20;  /* space */
    int32_t ch32 = 0;
    memcpy(&ch32, c, 4);
    return (int64_t)ch32;
}

int64_t nitty_grid_get_cell_fg(int64_t col, int64_t row)
{
    uint8_t *c = cell_ptr(col, row);
    if (c == NULL) return 0xCCCCCC;
    return (int64_t)((c[4] << 16) | (c[5] << 8) | c[6]);
}

int64_t nitty_grid_get_cell_bg(int64_t col, int64_t row)
{
    uint8_t *c = cell_ptr(col, row);
    if (c == NULL) return 0x1E1E1E;
    return (int64_t)((c[7] << 16) | (c[8] << 8) | c[9]);
}

void nitty_grid_clear(int64_t fg, int64_t bg)
{
    if (g_cells == NULL) return;

    for (int64_t r = 0; r < g_rows; r++) {
        for (int64_t c = 0; c < g_cols; c++) {
            nitty_grid_set_cell(c, r, 0x20, fg, bg);
        }
    }
}

/* ── Cursor ───────────────────────────────────────────────────────────── */

void nitty_grid_set_cursor(int64_t col, int64_t row)
{
    if (col >= 0 && col < g_cols) g_cursor_col = col;
    if (row >= 0 && row < g_rows) g_cursor_row = row;
}

int64_t nitty_grid_get_cursor_col(void) { return g_cursor_col; }
int64_t nitty_grid_get_cursor_row(void) { return g_cursor_row; }

void nitty_grid_set_cursor_style(int64_t style)
{
    if (style >= 0 && style <= 2) g_cursor_style = style;
}

int64_t nitty_grid_get_cursor_style(void)   { return g_cursor_style; }
int64_t nitty_grid_get_cursor_visible(void) { return g_cursor_visible; }

int64_t nitty_grid_toggle_cursor_blink(void)
{
    g_cursor_visible = g_cursor_visible ? 0 : 1;
    return g_cursor_visible;
}

/* ── Resize ───────────────────────────────────────────────────────────── */

int64_t nitty_grid_resize(int64_t new_width_px, int64_t new_height_px)
{
    int64_t new_cols = new_width_px / g_cell_w;
    int64_t new_rows = new_height_px / g_cell_h;
    if (new_cols < 1) new_cols = 1;
    if (new_rows < 1) new_rows = 1;

    /* No change? */
    if (new_cols == g_cols && new_rows == g_rows) return 1;

    /* Allocate new buffer */
    size_t new_size = (size_t)(new_cols * new_rows) * CELL_SIZE;
    uint8_t *new_cells = (uint8_t *)malloc(new_size);
    if (new_cells == NULL) return 0;

    /* Fill with spaces + default colors */
    int64_t dfg = (int64_t)((g_default_fg[0] << 16) | (g_default_fg[1] << 8) | g_default_fg[2]);
    int64_t dbg = (int64_t)((g_default_bg[0] << 16) | (g_default_bg[1] << 8) | g_default_bg[2]);

    /* Initialize all new cells to space */
    for (int64_t r = 0; r < new_rows; r++) {
        for (int64_t c = 0; c < new_cols; c++) {
            uint8_t *cell = &new_cells[(r * new_cols + c) * CELL_SIZE];
            int32_t sp = 0x20;
            memcpy(cell, &sp, 4);
            cell[4] = (uint8_t)((dfg >> 16) & 0xFF);
            cell[5] = (uint8_t)((dfg >> 8)  & 0xFF);
            cell[6] = (uint8_t)( dfg        & 0xFF);
            cell[7] = (uint8_t)((dbg >> 16) & 0xFF);
            cell[8] = (uint8_t)((dbg >> 8)  & 0xFF);
            cell[9] = (uint8_t)( dbg        & 0xFF);
        }
    }

    /* Copy existing content (overlapping area) */
    if (g_cells != NULL) {
        int64_t copy_cols = (new_cols < g_cols) ? new_cols : g_cols;
        int64_t copy_rows = (new_rows < g_rows) ? new_rows : g_rows;
        for (int64_t r = 0; r < copy_rows; r++) {
            for (int64_t c = 0; c < copy_cols; c++) {
                memcpy(
                    &new_cells[(r * new_cols + c) * CELL_SIZE],
                    &g_cells[(r * g_cols + c) * CELL_SIZE],
                    CELL_SIZE
                );
            }
        }
        free(g_cells);
    }

    g_cells = new_cells;
    g_cols = new_cols;
    g_rows = new_rows;

    /* Clamp cursor to new bounds */
    if (g_cursor_col >= g_cols) g_cursor_col = g_cols - 1;
    if (g_cursor_row >= g_rows) g_cursor_row = g_rows - 1;

    return 1;
}

/* ── Render ───────────────────────────────────────────────────────────── */

void nitty_grid_render(int64_t cr_ptr, int64_t width, int64_t height)
{
    if (g_cells == NULL || cr_ptr == 0) return;
    (void)width;
    (void)height;

    cairo_t *cr = (cairo_t *)(uintptr_t)cr_ptr;

    /* Clear surface to default BG */
    cairo_set_source_rgb(cr,
        (double)g_default_bg[0] / 255.0,
        (double)g_default_bg[1] / 255.0,
        (double)g_default_bg[2] / 255.0);
    cairo_paint(cr);

    /* Set up Pango */
    PangoLayout *layout = pango_cairo_create_layout(cr);
    PangoFontDescription *fd = pango_font_description_from_string(g_font_desc);
    pango_layout_set_font_description(layout, fd);

    char utf8_buf[8]; /* For single character rendering */

    for (int64_t r = 0; r < g_rows; r++) {
        for (int64_t c = 0; c < g_cols; c++) {
            uint8_t *cell = cell_ptr(c, r);
            if (cell == NULL) continue;

            int32_t ch = 0;
            memcpy(&ch, cell, 4);
            uint8_t fg_r = cell[4], fg_g = cell[5], fg_b = cell[6];
            uint8_t bg_r = cell[7], bg_g = cell[8], bg_b = cell[9];

            double px = (double)(c * g_cell_w);
            double py = (double)(r * g_cell_h);

            /* ── Cursor rendering ──────────────────────────────── */
            int is_cursor = (c == g_cursor_col && r == g_cursor_row && g_cursor_visible);

            if (is_cursor && g_cursor_style == 0) {
                /* Block cursor: swap fg/bg */
                uint8_t tmp;
                tmp = fg_r; fg_r = bg_r; bg_r = tmp;
                tmp = fg_g; fg_g = bg_g; bg_g = tmp;
                tmp = fg_b; fg_b = bg_b; bg_b = tmp;
            }

            /* Cell background */
            cairo_set_source_rgb(cr,
                (double)bg_r / 255.0,
                (double)bg_g / 255.0,
                (double)bg_b / 255.0);
            cairo_rectangle(cr, px, py, (double)g_cell_w, (double)g_cell_h);
            cairo_fill(cr);

            /* Cell character (skip spaces for performance) */
            if (ch > 0x20) {
                /* Convert codepoint to UTF-8 (ASCII for now) */
                if (ch < 0x80) {
                    utf8_buf[0] = (char)ch;
                    utf8_buf[1] = '\0';
                } else {
                    /* Basic UTF-8 encoding for BMP */
                    if (ch < 0x800) {
                        utf8_buf[0] = (char)(0xC0 | (ch >> 6));
                        utf8_buf[1] = (char)(0x80 | (ch & 0x3F));
                        utf8_buf[2] = '\0';
                    } else {
                        utf8_buf[0] = (char)(0xE0 | (ch >> 12));
                        utf8_buf[1] = (char)(0x80 | ((ch >> 6) & 0x3F));
                        utf8_buf[2] = (char)(0x80 | (ch & 0x3F));
                        utf8_buf[3] = '\0';
                    }
                }

                cairo_set_source_rgb(cr,
                    (double)fg_r / 255.0,
                    (double)fg_g / 255.0,
                    (double)fg_b / 255.0);
                cairo_move_to(cr, px, py);
                pango_layout_set_text(layout, utf8_buf, -1);
                pango_cairo_show_layout(cr, layout);
            }

            /* ── Cursor overlays (underline / bar) ───────────── */
            if (is_cursor && g_cursor_style == 1) {
                /* Underline: 2px line at bottom of cell */
                cairo_set_source_rgb(cr,
                    (double)fg_r / 255.0,
                    (double)fg_g / 255.0,
                    (double)fg_b / 255.0);
                cairo_rectangle(cr, px, py + (double)g_cell_h - 2.0,
                                (double)g_cell_w, 2.0);
                cairo_fill(cr);
            }
            if (is_cursor && g_cursor_style == 2) {
                /* Bar: 2px vertical line at left of cell */
                cairo_set_source_rgb(cr,
                    (double)fg_r / 255.0,
                    (double)fg_g / 255.0,
                    (double)fg_b / 255.0);
                cairo_rectangle(cr, px, py, 2.0, (double)g_cell_h);
                cairo_fill(cr);
            }
        }
    }

    pango_font_description_free(fd);
    g_object_unref(layout);
}
