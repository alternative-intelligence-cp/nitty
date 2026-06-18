/*
 * nitty_render.h — Terminal grid rendering for Nitty
 *
 * v0.3.2: Added nitty_render_set_cell_flags(), nitty_render_set_blink_visible(), nitty_render_has_blink_cells().
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
void nitty_render_append_cell_cp(int64_t col, int64_t row, int64_t codepoint);

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
 * Set per-cell attribute flags (bold, italic, underline, blink, etc.).
 * The flags bitmask matches cell.npk CELL_* constants exactly:
 *   bit 0: bold, bit 2: italic, bit 3: underline, bit 4: blink,
 *   bit 5: rapid_blink, bit 8: strikethrough, bit 9: overline.
 *   bits 12-14: underline style (0=single,1=double,2=curly,3=dotted,4=dashed).
 */
void nitty_render_set_cell_flags(int64_t col, int64_t row, int64_t flags);

/**
 * Set the blink visibility state.
 * @param visible  1 = draw blinking cells, 0 = hide blinking cells.
 * Called by the blink timer in nitty_gtk4_shim.c at 500ms intervals.
 */
void nitty_render_set_blink_visible(int64_t visible);

/**
 * Return 1 if any cell in the current grid has a blink or rapid_blink flag, 0 otherwise.
 * Called by the shim idle callback to decide whether to start/stop the blink timer.
 */
int64_t nitty_render_has_blink_cells(void);

/**
 * Render the grid. Called internally by the draw callback.
 * @param cr      Cairo context (from on_draw_func)
 * @param width   DrawingArea width
 * @param height  DrawingArea height
 */
void nitty_render_frame(void *cr, int width, int height);

/* ── v0.3.3: Cursor rendering ─────────────────────────────────────────── */

/**
 * Push cursor state for the next frame.
 * Called from renderer_sync_frame() every draw cycle.
 *
 * @param col      Cursor column (0-based).
 * @param row      Cursor row (0-based).
 * @param shape    0=Block, 1=Underline, 2=Bar.
 * @param visible  1 if cursor should be drawn this frame (accounts for
 *                 DECTCEM and the blink phase).
 * @param focused  1 if terminal has focus (filled), 0 = hollow outline.
 */
void nitty_render_set_cursor(int64_t col, int64_t row,
                              int64_t shape, int64_t visible,
                              int64_t focused);

/**
 * Get current cursor blink phase.
 * @return 1 = visible phase, 0 = hidden phase.
 * Read by renderer.npk each frame to compute cursor visibility.
 */
int64_t nitty_render_get_cursor_blink_phase(void);

/**
 * Set cursor blink phase.
 * @param phase  1 = visible, 0 = hidden.
 * Called by the 530ms cursor blink timer in nitty_gtk4_shim.c.
 */
void nitty_render_set_cursor_blink_phase(int64_t phase);

/**
 * Set scrollback scroll info for overlay scrollbar rendering (v0.3.4).
 * @param offset   Current scroll offset (rows from bottom; 0 = at bottom).
 * @param total    Total rows: scrollback_len + visible_rows.
 * @param visible  Visible rows on screen.
 * Called by renderer.npk each frame.
 */
void nitty_render_set_scroll_info(int64_t offset, int64_t total, int64_t visible);

/* ── v0.3.5: Font system ──────────────────────────────────────────────── */

/**
 * Set the line height multiplier (fixed-point × 1000).
 * 1000 = 1.0× (default — cell height equals font height).
 * 1200 = 1.2× (20% extra vertical spacing).
 * Triggers a font re-measurement on the next frame.
 */
void nitty_render_set_line_height(int64_t lh_x1000);

/**
 * Set additional horizontal letter spacing in pixels (default: 0).
 * Applied in addition to the natural character advance.
 * Triggers a font re-measurement on the next frame.
 */
void nitty_render_set_letter_spacing(int64_t px);

/**
 * Enable or disable Pango font ligatures (default: disabled).
 * @param enabled  1 = allow ligatures (default Pango/HarfBuzz behavior).
 *                 0 = disable liga, calt, dlig features via font-features attr.
 */
void nitty_render_set_ligatures(int64_t enabled);

/* ── v0.3.6: Performance stats ────────────────────────────────────────── */

/**
 * Get the number of glyph atlas cache hits since last reset.
 * A hit means the character was already cached in the atlas.
 */
int64_t nitty_render_get_atlas_hits(void);

/**
 * Get the number of glyph atlas cache misses since last reset.
 * A miss means the character had to be rendered via Pango and cached.
 */
int64_t nitty_render_get_atlas_misses(void);

/**
 * Get the number of dirty (changed) cells in the last frame.
 * Lower is better — indicates how much work the partial redraw saved.
 */
int64_t nitty_render_get_dirty_cells(void);

/**
 * Get the total number of visible cells in the last frame.
 */
int64_t nitty_render_get_total_cells(void);

/**
 * Reset all performance counters to zero.
 */
void nitty_render_reset_perf_stats(void);

/* ── v0.4.0: Tab bar rendering ────────────────────────────────────────── */

/**
 * Set the pixel height of the tab bar region at the top of the DrawingArea.
 * The terminal grid is rendered starting at y = tab_bar_height.
 * Set to 0 to disable the tab bar (default).
 */
void nitty_render_set_tab_bar_height(int64_t height);

/**
 * Get the current tab bar height offset.
 */
int64_t nitty_render_get_tab_bar_height(void);

/**
 * Draw a filled rectangle in the tab bar region.
 * Used for tab backgrounds. Colors are fixed-point [0..1000].
 */
void nitty_render_draw_tab_bg(int64_t x, int64_t y, int64_t w, int64_t h,
                               int64_t r, int64_t g, int64_t b);

/**
 * Draw text in the tab bar region using the terminal font.
 * @param x, y   Position (pixels)
 * @param text   UTF-8 text to draw
 * @param r,g,b  Text color (fixed-point [0..1000])
 * @param bold   1 = render bold, 0 = normal weight
 */
void nitty_render_draw_tab_text(int64_t x, int64_t y, const char *text,
                                 int64_t r, int64_t g, int64_t b,
                                 int64_t bold);

/**
 * Draw an accent underline in the tab bar.
 * @param x, y   Left edge, baseline Y
 * @param w      Width in pixels
 * @param r,g,b  Underline color (fixed-point [0..1000])
 */
void nitty_render_draw_tab_underline(int64_t x, int64_t y, int64_t w,
                                      int64_t r, int64_t g, int64_t b);

/**
 * Draw a vertical separator line between tabs.
 * @param x      X position
 * @param y      Top Y
 * @param h      Height in pixels
 */
void nitty_render_draw_tab_separator(int64_t x, int64_t y, int64_t h);

/**
 * v0.4.3: Draw a filled circle (6px diameter) for activity indicator dots.
 * x, y: centre of the circle in pixels.
 * r, g, b: colour components in thousandths (0-1000).
 */
void nitty_render_draw_dot(int64_t x, int64_t y, int64_t r, int64_t g, int64_t b);

#ifdef __cplusplus
}
#endif

#endif /* NITTY_RENDER_H */
