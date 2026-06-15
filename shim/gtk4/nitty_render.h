/*
 * nitty_render.h — Terminal grid rendering for Nitty
 *
 * v0.3.0: Added nitty_render_clear_grid() and nitty_render_get_font_baseline().
 * Nitpick configures the render state, then the C draw callback paints it.
 *
 * Design:
 *   - Nitpick sets the font, grid dimensions, cell contents, and colors
 *     via the configure/set functions BEFORE app_run()
 *   - The internal C render function (called by on_draw_func) reads this
 *     state and paints the grid
 *   - No Nitpick function pointers are needed
 */

#ifndef NITTY_RENDER_H
#define NITTY_RENDER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum grid dimensions */
#define NITTY_MAX_COLS 512
#define NITTY_MAX_ROWS 256

/* ── Grid configuration ───────────────────────────────────────────────── */

/**
 * Set the font for the terminal grid.
 * @param font_desc  Pango font description (e.g. "Monospace 12")
 */
void nitty_render_set_font(const char *font_desc);

/**
 * Set the background color (fixed-point × 1000, [0..1000] → [0.0..1.0]).
 */
void nitty_render_set_bg(int64_t r, int64_t g, int64_t b);

/**
 * Set the foreground (text) color.
 */
void nitty_render_set_fg(int64_t r, int64_t g, int64_t b);

/**
 * Set a character at a specific grid position using a raw codepoint.
 * Encodes the Unicode codepoint as UTF-8 internally.
 * This is the preferred method from Nitpick (avoids dynamic string construction).
 * @param col        Column (0-based)
 * @param row        Row (0-based)
 * @param codepoint  Unicode codepoint (U+0000 to U+10FFFF)
 */
void nitty_render_set_cell_cp(int64_t col, int64_t row, int64_t codepoint);

/**
 * Set a character at a specific grid position.
 * Characters beyond the grid bounds are silently ignored.
 * @param col   Column (0-based)
 * @param row   Row (0-based)
 * @param ch    Character string (1 character + NUL, or multi-byte UTF-8)
 */
void nitty_render_set_cell(int64_t col, int64_t row, const char *ch);

/**
 * Set a per-cell foreground color (fixed-point × 1000).
 * If not set, the default foreground color is used.
 */
void nitty_render_set_cell_fg(int64_t col, int64_t row,
                               int64_t r, int64_t g, int64_t b);

/**
 * Set a per-cell background color (fixed-point × 1000).
 * If not set, the default background color is used.
 */
void nitty_render_set_cell_bg(int64_t col, int64_t row,
                               int64_t r, int64_t g, int64_t b);

/**
 * Fill the grid with a repeating string (convenience for demo/test).
 * @param text  String to repeat across the grid
 * @param cols  Number of columns to fill
 * @param rows  Number of rows to fill
 */
void nitty_render_fill_text(const char *text, int64_t cols, int64_t rows);

/**
 * Get the computed cell width in pixels (after font measurement).
 * Returns 0 if font hasn't been measured yet.
 */
int64_t nitty_render_get_cell_width(void);

/**
 * Get the computed cell height in pixels (after font measurement).
 * Returns 0 if font hasn't been measured yet.
 */
int64_t nitty_render_get_cell_height(void);

/**
 * Get the font baseline offset in fixed-point × 1000.
 * This is the vertical distance from cell top to where text ink starts.
 * Returns 0 if font hasn't been measured yet.
 */
int64_t nitty_render_get_font_baseline(void);

/**
 * Clear all grid cells to empty (space with no color overrides).
 * Call before each frame sync to prevent stale characters from persisting.
 */
void nitty_render_clear_grid(void);

/**
 * Render the grid. Called internally by the draw callback.
 * @param cr      Cairo context (from on_draw_func)
 * @param width   DrawingArea width
 * @param height  DrawingArea height
 */
void nitty_render_frame(void *cr, int width, int height);

#ifdef __cplusplus
}
#endif

#endif /* NITTY_RENDER_H */
