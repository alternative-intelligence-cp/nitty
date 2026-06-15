/*
 * nitty_grid.h — Terminal grid data structure for Nitty
 *
 * v0.0.4: Cell-based 2D grid for terminal emulation.
 *
 * Each cell stores:
 *   - character (int32_t, supports ASCII and future Unicode)
 *   - foreground color (3x uint8_t: R, G, B)
 *   - background color (3x uint8_t: R, G, B)
 *   - attributes (uint8_t: bold, underline, etc. — future)
 *
 * Cell layout: 10 bytes per cell (4 char + 3 fg + 3 bg)
 * Grid buffer: heap-allocated via malloc, flat row-major order.
 *
 * Cursor state is part of the grid — position, style, blink visibility.
 * Cursor rendering is handled by the render module, not here.
 */

#ifndef NITTY_GRID_H
#define NITTY_GRID_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Grid lifecycle ───────────────────────────────────────────────────── */

/**
 * Initialize the grid for a given pixel size and font.
 * Calculates cols/rows from pixel dimensions and font metrics.
 * @param width_px   Window width in pixels
 * @param height_px  Window height in pixels
 * @param font_desc  Pango font description string (e.g. "Monospace 12")
 * @return  1 on success, 0 on failure
 */
int64_t nitty_grid_init(int64_t width_px, int64_t height_px, const char *font_desc);

/**
 * Destroy the grid and free the cell buffer.
 */
void nitty_grid_destroy(void);

/* ── Grid dimensions ──────────────────────────────────────────────────── */

int64_t nitty_grid_get_cols(void);
int64_t nitty_grid_get_rows(void);
int64_t nitty_grid_get_cell_width(void);
int64_t nitty_grid_get_cell_height(void);

/* ── Cell access ──────────────────────────────────────────────────────── */

/**
 * Set a cell's character and colors.
 * @param col  Column (0-indexed)
 * @param row  Row (0-indexed)
 * @param ch   Character code (ASCII / Unicode codepoint)
 * @param fg   Foreground color packed as 0xRRGGBB
 * @param bg   Background color packed as 0xRRGGBB
 */
void nitty_grid_set_cell(int64_t col, int64_t row, int64_t ch, int64_t fg, int64_t bg);

/** Get the character at (col, row). Returns ' ' (0x20) for out-of-bounds. */
int64_t nitty_grid_get_cell_char(int64_t col, int64_t row);

/** Get the FG color at (col, row) as packed 0xRRGGBB. */
int64_t nitty_grid_get_cell_fg(int64_t col, int64_t row);

/** Get the BG color at (col, row) as packed 0xRRGGBB. */
int64_t nitty_grid_get_cell_bg(int64_t col, int64_t row);

/** Clear the entire grid to spaces with given fg/bg colors. */
void nitty_grid_clear(int64_t fg, int64_t bg);

/* ── Cursor ───────────────────────────────────────────────────────────── */

void    nitty_grid_set_cursor(int64_t col, int64_t row);
int64_t nitty_grid_get_cursor_col(void);
int64_t nitty_grid_get_cursor_row(void);

/** Set cursor style: 0=block, 1=underline, 2=bar. */
void    nitty_grid_set_cursor_style(int64_t style);
int64_t nitty_grid_get_cursor_style(void);

/** Toggle cursor blink visibility. Returns new visibility (0 or 1). */
int64_t nitty_grid_toggle_cursor_blink(void);
int64_t nitty_grid_get_cursor_visible(void);

/* ── Resize ───────────────────────────────────────────────────────────── */

/**
 * Resize the grid for new pixel dimensions.
 * Preserves existing cell content where possible.
 * @return 1 on success, 0 on failure
 */
int64_t nitty_grid_resize(int64_t new_width_px, int64_t new_height_px);

/* ── Render integration ───────────────────────────────────────────────── */

/**
 * Render the entire grid to a Cairo context via Pango.
 * Called from the GTK draw callback.
 * @param cr       Cairo context pointer (as int64)
 * @param width    Drawable area width in pixels
 * @param height   Drawable area height in pixels
 */
void nitty_grid_render(int64_t cr_ptr, int64_t width, int64_t height);

#ifdef __cplusplus
}
#endif

#endif /* NITTY_GRID_H */
