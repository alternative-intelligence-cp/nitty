/*
 * nitty_render.c — Terminal grid renderer for Nitty
 *
 * v0.3.5: Font system — line height, letter spacing, ligature toggle (Pango
 *         font features), wide character (double-cell) rendering.
 * v0.3.2: Bold/italic font variants, underline styles (single/double/curly/
 *         dotted/dashed), strikethrough, overline, blink suppression.
 * v0.0.2: C-side rendering of a monospace character grid using Cairo + Pango.
 *
 * Architecture:
 *   - Nitpick sets font, colors, grid content, and cell flags via nitty_render_*()
 *   - The GTK4 draw callback (on_draw_func) calls nitty_render_frame()
 *   - nitty_render_frame() uses stored state to paint the grid
 *   - A single PangoLayout is reused per frame for performance
 *   - Font metrics (cell width/height) are computed once per font change
 *
 * Coordinates:
 *   - All grid positions are in logical pixels (GTK handles HiDPI scaling)
 *   - Colors use fixed-point × 1000: 0=0.0, 500=0.5, 1000=1.0
 *
 * Cell flags bitmask (matches cell.npk CELL_* constants):
 *   bit 0:  bold          (0x001)
 *   bit 1:  dim           (0x002) — handled by color_resolve, not font
 *   bit 2:  italic        (0x004)
 *   bit 3:  underline     (0x008)
 *   bit 4:  blink         (0x010)
 *   bit 5:  rapid_blink   (0x020)
 *   bit 6:  inverse       (0x040) — handled by color_resolve
 *   bit 7:  hidden        (0x080) — handled by color_resolve
 *   bit 8:  strikethrough (0x100)
 *   bit 9:  overline      (0x200)
 *   bits 12-14: underline style (0=single,1=double,2=curly,3=dotted,4=dashed)
 */

#include "nitty_render.h"
#include <gtk/gtk.h>
#include <pango/pangocairo.h>
#include <string.h>
#include <stdlib.h>

/* ── Flag constants (mirroring cell.npk CELL_* values) ───────────────── */

#define FLAG_BOLD         0x001
#define FLAG_ITALIC       0x004
#define FLAG_UNDERLINE    0x008
#define FLAG_BLINK        0x010
#define FLAG_RAPID_BLINK  0x020
#define FLAG_STRIKE       0x100
#define FLAG_OVERLINE     0x200
#define FLAG_WIDE         0x400  /* v0.3.5: cell is 2 columns wide */
#define FLAG_CONT         0x800  /* v0.3.5: cell is 2nd col of a wide char */
#define FLAG_UL_STYLE_SHIFT 12
#define FLAG_UL_STYLE_MASK  0x7000

#define UL_SINGLE  0
#define UL_DOUBLE  1
#define UL_CURLY   2
#define UL_DOTTED  3
#define UL_DASHED  4

/* ── Grid cell structure ──────────────────────────────────────────────── */

typedef struct {
    char ch[8];       /* UTF-8 character (up to 4 bytes + NUL) */
    int  has_fg;      /* Non-zero if per-cell foreground is set */
    int  has_bg;      /* Non-zero if per-cell background is set */
    int  fg_r, fg_g, fg_b;  /* Per-cell foreground (fixed-point × 1000) */
    int  bg_r, bg_g, bg_b;  /* Per-cell background (fixed-point × 1000) */
    int  flags;       /* v0.3.2: CELL_* attribute bitmask */
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
static int      g_font_baseline = 0;

/* v0.3.2: Four Pango font descriptions for bold/italic variants */
static PangoFontDescription *g_font_regular    = NULL;
static PangoFontDescription *g_font_bold       = NULL;
static PangoFontDescription *g_font_italic     = NULL;
static PangoFontDescription *g_font_bold_italic = NULL;
static int g_bold_wider = 0;  /* 1 if bold font wider than regular cell width */

/* Deprecated single-font pointer (kept for compat, points to g_font_regular) */
static PangoFontDescription *g_cached_font = NULL;

/* v0.3.2: Blink state */
static int g_blink_visible   = 1; /* 1=show blinking cells, 0=hide them */
static int g_has_blink_cells = 0; /* updated each frame */

/* v0.3.3: Cursor state — updated each frame by nitty_render_set_cursor() */
static int g_cursor_col     = 0;
static int g_cursor_row     = 0;
static int g_cursor_shape   = 0; /* 0=Block, 1=Underline, 2=Bar */
static int g_cursor_visible = 1; /* accounts for DECTCEM + blink phase */
static int g_cursor_focused = 1; /* 1=focused (filled), 0=unfocused (hollow) */
static int g_cursor_blink_phase = 1; /* 1=visible, 0=hidden — toggled by 530ms timer */

/* v0.3.4: Scrollbar state — updated each frame by nitty_render_set_scroll_info() */
static int64_t g_scroll_offset  = 0;   /* rows scrolled up from bottom */
static int64_t g_scroll_total   = 0;   /* total rows (scrollback + visible) */
static int64_t g_scroll_visible = 0;   /* visible rows on screen */

/* v0.3.5: Font system globals */
static int     g_line_height_x1000 = 1000; /* 1000 = 1.0× (no extra spacing) */
static int     g_letter_spacing    = 0;    /* additional px per cell (default 0) */
static int     g_ligatures_enabled = 0;    /* 0 = disable (default), 1 = enable */
/* Cached Pango attribute list for ligature suppression (NULL when enabled) */
static PangoAttrList *g_no_ligature_attrs = NULL;

/* ── v0.3.6: Glyph Atlas Cache ────────────────────────────────────────── */

#define ATLAS_W          2048
#define ATLAS_H          2048
#define ATLAS_BUCKETS    4096   /* hash table size (power of 2) */
#define ATLAS_EMPTY_KEY  0xFFFFFFFFU  /* sentinel for empty bucket */

typedef struct {
    uint32_t codepoint;   /* Unicode codepoint */
    uint16_t flags_key;   /* bold|italic|wide bits → 3 bits, 8 combos */
    uint16_t fg_packed;   /* (r/8)<<10 | (g/8)<<5 | (b/8) — 15-bit color */
} GlyphKey;

typedef struct {
    GlyphKey key;
    int      ax, ay;      /* position in atlas surface */
    int      gw, gh;      /* glyph pixel dimensions */
} GlyphEntry;

static cairo_surface_t *g_atlas_surface  = NULL;
static cairo_t         *g_atlas_cr       = NULL;
static GlyphEntry       g_atlas_entries[ATLAS_BUCKETS]; /* open-addressing hash table */
static int              g_atlas_count    = 0;           /* entries in use */
static int              g_atlas_next_x   = 0;           /* packing cursor */
static int              g_atlas_next_y   = 0;
static int              g_atlas_row_h    = 0;           /* tallest glyph in current row */

/* ── v0.3.6: Damage Tracking (double-buffer diff) ────────────────────── */

static GridCell g_prev_grid[NITTY_MAX_ROWS][NITTY_MAX_COLS];
static int      g_prev_cols = 0, g_prev_rows = 0;
static uint8_t  g_dirty[NITTY_MAX_ROWS][NITTY_MAX_COLS];
static int      g_full_redraw   = 1;  /* force first frame */
static int      g_prev_cursor_col = -1, g_prev_cursor_row = -1;

/* ── v0.3.6: Performance counters ─────────────────────────────────────── */

static int64_t  g_perf_atlas_hits   = 0;
static int64_t  g_perf_atlas_misses = 0;
static int64_t  g_perf_dirty_cells  = 0;
static int64_t  g_perf_total_cells  = 0;

/* Forward declaration for draw_scrollbar */
static void draw_scrollbar(cairo_t *cr, int width, int height);

/* ── Helper: free all font variants ───────────────────────────────────── */

static void free_font_variants(void)
{
    if (g_font_regular)     { pango_font_description_free(g_font_regular);     g_font_regular     = NULL; }
    if (g_font_bold)        { pango_font_description_free(g_font_bold);        g_font_bold        = NULL; }
    if (g_font_italic)      { pango_font_description_free(g_font_italic);      g_font_italic      = NULL; }
    if (g_font_bold_italic) { pango_font_description_free(g_font_bold_italic); g_font_bold_italic = NULL; }
    g_cached_font = NULL;
}

/* ── Helper: measure font metrics ─────────────────────────────────────── */

static void measure_font(cairo_t *cr)
{
    if (!g_font_changed) return;

    free_font_variants();

    /* Parse base font description */
    PangoFontDescription *base = pango_font_description_from_string(g_font_desc_str);
    if (base == NULL) return;

    /* Regular */
    g_font_regular = pango_font_description_copy(base);

    /* Bold */
    g_font_bold = pango_font_description_copy(base);
    pango_font_description_set_weight(g_font_bold, PANGO_WEIGHT_BOLD);

    /* Italic */
    g_font_italic = pango_font_description_copy(base);
    pango_font_description_set_style(g_font_italic, PANGO_STYLE_ITALIC);

    /* Bold-Italic */
    g_font_bold_italic = pango_font_description_copy(base);
    pango_font_description_set_weight(g_font_bold_italic, PANGO_WEIGHT_BOLD);
    pango_font_description_set_style(g_font_bold_italic, PANGO_STYLE_ITALIC);

    pango_font_description_free(base);

    g_cached_font = g_font_regular; /* compat alias */

    /* Measure regular cell dimensions */
    PangoLayout *layout = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(layout, g_font_regular);
    pango_layout_set_text(layout, "M", 1);

    int char_w = 0, char_h = 0;
    pango_layout_get_pixel_size(layout, &char_w, &char_h);

    PangoRectangle ink_rect;
    pango_layout_get_pixel_extents(layout, &ink_rect, NULL);
    g_object_unref(layout);

    if (char_w > 0 && char_h > 0) {
        g_cell_width  = char_w + g_letter_spacing;
        /* v0.3.5: apply line height multiplier */
        int raw_h     = char_h;
        g_cell_height = (raw_h * g_line_height_x1000 + 500) / 1000; /* round */
        if (g_cell_height < raw_h) g_cell_height = raw_h; /* never shorter than font */
        /* Adjust baseline to center text vertically in the taller cell */
        int extra     = g_cell_height - raw_h;
        g_font_baseline = (ink_rect.y < 0) ? (-ink_rect.y * 1000) : 0;
        g_font_baseline += (extra / 2) * 1000; /* center vertically */
    }

    /* Check if bold variant overflows cell width */
    if (g_font_bold != NULL && g_cell_width > 0) {
        PangoLayout *bl = pango_cairo_create_layout(cr);
        pango_layout_set_font_description(bl, g_font_bold);
        pango_layout_set_text(bl, "M", 1);
        int bw = 0, bh = 0;
        pango_layout_get_pixel_size(bl, &bw, &bh);
        g_object_unref(bl);
        g_bold_wider = (bw > g_cell_width) ? 1 : 0;
    }

    g_font_changed = 0;
}

/* ── Helper: select font variant for flags ────────────────────────────── */

static PangoFontDescription *select_font(int flags)
{
    int bold   = (flags & FLAG_BOLD)   != 0;
    int italic = (flags & FLAG_ITALIC) != 0;
    if (bold && italic) return g_font_bold_italic ? g_font_bold_italic : g_font_regular;
    if (bold)           return g_font_bold        ? g_font_bold        : g_font_regular;
    if (italic)         return g_font_italic      ? g_font_italic      : g_font_regular;
    return g_font_regular;
}

/* ═══════════════════════════════════════════════════════════════════════
 * v0.3.6: Glyph Atlas Implementation
 * ═══════════════════════════════════════════════════════════════════════ */

static void glyph_atlas_init(cairo_t *screen_cr)
{
    (void)screen_cr; /* used only as fallback; atlas has its own cr */
    if (g_atlas_surface != NULL) return;
    g_atlas_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, ATLAS_W, ATLAS_H);
    g_atlas_cr = cairo_create(g_atlas_surface);
    /* Clear atlas to transparent */
    cairo_set_operator(g_atlas_cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(g_atlas_cr, 0, 0, 0, 0);
    cairo_paint(g_atlas_cr);
    cairo_set_operator(g_atlas_cr, CAIRO_OPERATOR_OVER);
    /* Initialize hash table with empty sentinel */
    for (int i = 0; i < ATLAS_BUCKETS; i++) {
        g_atlas_entries[i].key.codepoint = ATLAS_EMPTY_KEY;
    }
    g_atlas_count  = 0;
    g_atlas_next_x = 0;
    g_atlas_next_y = 0;
    g_atlas_row_h  = 0;
}

static void glyph_atlas_clear(void)
{
    if (g_atlas_cr != NULL) {
        /* Clear to transparent */
        cairo_set_operator(g_atlas_cr, CAIRO_OPERATOR_SOURCE);
        cairo_set_source_rgba(g_atlas_cr, 0, 0, 0, 0);
        cairo_paint(g_atlas_cr);
        cairo_set_operator(g_atlas_cr, CAIRO_OPERATOR_OVER);
    }
    for (int i = 0; i < ATLAS_BUCKETS; i++) {
        g_atlas_entries[i].key.codepoint = ATLAS_EMPTY_KEY;
    }
    g_atlas_count  = 0;
    g_atlas_next_x = 0;
    g_atlas_next_y = 0;
    g_atlas_row_h  = 0;
}

static uint32_t glyph_key_hash(GlyphKey k)
{
    /* FNV-1a */
    uint32_t h = 2166136261u;
    h ^= k.codepoint; h *= 16777619u;
    h ^= k.flags_key; h *= 16777619u;
    h ^= k.fg_packed;  h *= 16777619u;
    return h;
}

static int glyph_key_eq(GlyphKey a, GlyphKey b)
{
    return a.codepoint == b.codepoint &&
           a.flags_key == b.flags_key &&
           a.fg_packed  == b.fg_packed;
}

static GlyphKey make_glyph_key(const GridCell *cell)
{
    GlyphKey k;
    uint32_t cp = 0;
    /* Decode first UTF-8 codepoint from cell->ch */
    const unsigned char *s = (const unsigned char *)cell->ch;
    if (s[0] < 0x80)       cp = s[0];
    else if (s[0] < 0xE0)  cp = ((s[0] & 0x1F) << 6) | (s[1] & 0x3F);
    else if (s[0] < 0xF0)  cp = ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
    else                    cp = ((s[0] & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
    k.codepoint = cp;

    /* flags_key: 3 bits → bold|italic|wide */
    k.flags_key = (uint16_t)(
        ((cell->flags & FLAG_BOLD)   ? 1 : 0) |
        ((cell->flags & FLAG_ITALIC) ? 2 : 0) |
        ((cell->flags & FLAG_WIDE)   ? 4 : 0)
    );

    /* fg_packed: 15-bit quantized color */
    int fr = cell->has_fg ? cell->fg_r : g_fg_r;
    int fg = cell->has_fg ? cell->fg_g : g_fg_g;
    int fb = cell->has_fg ? cell->fg_b : g_fg_b;
    k.fg_packed = (uint16_t)(((fr / 8) << 10) | ((fg / 8) << 5) | (fb / 8));

    return k;
}

/* Lookup: returns pointer to entry or NULL if not found */
static GlyphEntry *glyph_atlas_lookup(GlyphKey key)
{
    uint32_t idx = glyph_key_hash(key) & (ATLAS_BUCKETS - 1);
    for (int probe = 0; probe < ATLAS_BUCKETS; probe++) {
        GlyphEntry *e = &g_atlas_entries[idx];
        if (e->key.codepoint == ATLAS_EMPTY_KEY) return NULL; /* empty slot = not found */
        if (glyph_key_eq(e->key, key)) return e;
        idx = (idx + 1) & (ATLAS_BUCKETS - 1);
    }
    return NULL;
}

/* Render a glyph to the atlas surface, insert into hash table, return entry */
static GlyphEntry *glyph_atlas_render(GlyphKey key, const GridCell *cell, cairo_t *screen_cr)
{
    if (g_atlas_cr == NULL) glyph_atlas_init(screen_cr);

    /* If atlas hash table is full, clear and start over */
    if (g_atlas_count >= ATLAS_BUCKETS * 3 / 4) {
        glyph_atlas_clear();
    }

    int is_wide = (key.flags_key & 4) ? 1 : 0;
    int glyph_w = is_wide ? (2 * g_cell_width) : g_cell_width;
    int glyph_h = g_cell_height;

    /* Check if we have room in current row */
    if (g_atlas_next_x + glyph_w > ATLAS_W) {
        /* Move to next row */
        g_atlas_next_x  = 0;
        g_atlas_next_y += g_atlas_row_h;
        g_atlas_row_h   = 0;
    }
    if (g_atlas_next_y + glyph_h > ATLAS_H) {
        /* Atlas surface full — clear and restart */
        glyph_atlas_clear();
    }

    int ax = g_atlas_next_x;
    int ay = g_atlas_next_y;

    /* Render glyph to atlas surface using Pango */
    PangoLayout *lo = pango_cairo_create_layout(g_atlas_cr);
    pango_layout_set_font_description(lo, select_font(cell->flags));
    if (!g_ligatures_enabled && g_no_ligature_attrs != NULL) {
        pango_layout_set_attributes(lo, g_no_ligature_attrs);
    }
    if (is_wide) {
        pango_layout_set_width(lo, glyph_w * PANGO_SCALE);
        pango_layout_set_alignment(lo, PANGO_ALIGN_CENTER);
    }
    pango_layout_set_text(lo, cell->ch, -1);

    /* Set text color */
    int fr = cell->has_fg ? cell->fg_r : g_fg_r;
    int fg = cell->has_fg ? cell->fg_g : g_fg_g;
    int fb = cell->has_fg ? cell->fg_b : g_fg_b;
    cairo_set_source_rgb(g_atlas_cr, (double)fr / 1000.0, (double)fg / 1000.0, (double)fb / 1000.0);

    cairo_move_to(g_atlas_cr, (double)ax, (double)ay);
    pango_cairo_show_layout(g_atlas_cr, lo);

    /* Synthetic bold: draw twice offset by 0.5px */
    if ((cell->flags & FLAG_BOLD) && g_bold_wider) {
        cairo_move_to(g_atlas_cr, (double)ax + 0.5, (double)ay);
        pango_cairo_show_layout(g_atlas_cr, lo);
    }

    g_object_unref(lo);

    /* Insert into hash table */
    uint32_t idx = glyph_key_hash(key) & (ATLAS_BUCKETS - 1);
    while (g_atlas_entries[idx].key.codepoint != ATLAS_EMPTY_KEY) {
        idx = (idx + 1) & (ATLAS_BUCKETS - 1);
    }
    g_atlas_entries[idx].key = key;
    g_atlas_entries[idx].ax  = ax;
    g_atlas_entries[idx].ay  = ay;
    g_atlas_entries[idx].gw  = glyph_w;
    g_atlas_entries[idx].gh  = glyph_h;
    g_atlas_count++;

    /* Advance packing cursor */
    g_atlas_next_x += glyph_w;
    if (glyph_h > g_atlas_row_h) g_atlas_row_h = glyph_h;

    return &g_atlas_entries[idx];
}

/* Get: lookup or render */
static GlyphEntry *glyph_atlas_get(GlyphKey key, const GridCell *cell, cairo_t *screen_cr)
{
    GlyphEntry *e = glyph_atlas_lookup(key);
    if (e != NULL) {
        g_perf_atlas_hits++;
        return e;
    }
    g_perf_atlas_misses++;
    return glyph_atlas_render(key, cell, screen_cr);
}

/* Blit a glyph from atlas to screen */
static void glyph_atlas_blit(GlyphEntry *e, cairo_t *cr, double dest_x, double dest_y)
{
    if (g_atlas_surface == NULL) return;
    cairo_save(cr);
    cairo_rectangle(cr, dest_x, dest_y, (double)e->gw, (double)e->gh);
    cairo_clip(cr);
    cairo_set_source_surface(cr, g_atlas_surface,
                             dest_x - (double)e->ax,
                             dest_y - (double)e->ay);
    cairo_paint(cr);
    cairo_restore(cr);
}

/* ═══════════════════════════════════════════════════════════════════════
 * v0.3.6: Damage Tracking Implementation
 * ═══════════════════════════════════════════════════════════════════════ */

static int cell_eq(const GridCell *a, const GridCell *b)
{
    return memcmp(a->ch, b->ch, sizeof(a->ch)) == 0 &&
           a->has_fg == b->has_fg && a->has_bg == b->has_bg &&
           a->fg_r == b->fg_r && a->fg_g == b->fg_g && a->fg_b == b->fg_b &&
           a->bg_r == b->bg_r && a->bg_g == b->bg_g && a->bg_b == b->bg_b &&
           a->flags == b->flags;
}

static void damage_compute(void)
{
    g_perf_dirty_cells = 0;
    g_perf_total_cells = 0;

    if (g_full_redraw) {
        /* Mark everything dirty */
        memset(g_dirty, 1, sizeof(g_dirty));
        int total = g_grid_rows * g_grid_cols;
        g_perf_dirty_cells = total;
        g_perf_total_cells = total;
        return;
    }

    /* Dimension change → full redraw */
    if (g_grid_cols != g_prev_cols || g_grid_rows != g_prev_rows) {
        g_full_redraw = 1;
        memset(g_dirty, 1, sizeof(g_dirty));
        int total = g_grid_rows * g_grid_cols;
        g_perf_dirty_cells = total;
        g_perf_total_cells = total;
        return;
    }

    /* Differential: compare cell-by-cell */
    memset(g_dirty, 0, sizeof(g_dirty));
    int rows = g_grid_rows < NITTY_MAX_ROWS ? g_grid_rows : NITTY_MAX_ROWS;
    int cols = g_grid_cols < NITTY_MAX_COLS ? g_grid_cols : NITTY_MAX_COLS;
    g_perf_total_cells = rows * cols;

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            if (!cell_eq(&g_grid[r][c], &g_prev_grid[r][c])) {
                g_dirty[r][c] = 1;
                g_perf_dirty_cells++;
            }
        }
    }

    /* Mark old and new cursor positions dirty (cursor moves without cell changes) */
    if (g_prev_cursor_row >= 0 && g_prev_cursor_row < rows &&
        g_prev_cursor_col >= 0 && g_prev_cursor_col < cols) {
        if (!g_dirty[g_prev_cursor_row][g_prev_cursor_col]) {
            g_dirty[g_prev_cursor_row][g_prev_cursor_col] = 1;
            g_perf_dirty_cells++;
        }
    }
    if (g_cursor_row >= 0 && g_cursor_row < rows &&
        g_cursor_col >= 0 && g_cursor_col < cols) {
        if (!g_dirty[g_cursor_row][g_cursor_col]) {
            g_dirty[g_cursor_row][g_cursor_col] = 1;
            g_perf_dirty_cells++;
        }
    }
}

static void damage_snapshot(void)
{
    memcpy(g_prev_grid, g_grid, sizeof(g_grid));
    g_prev_cols = g_grid_cols;
    g_prev_rows = g_grid_rows;
    g_prev_cursor_col = g_cursor_col;
    g_prev_cursor_row = g_cursor_row;
    g_full_redraw = 0;
}

/* ═══════════════════════════════════════════════════════════════════════
 * v0.3.6: Performance Stats API
 * ═══════════════════════════════════════════════════════════════════════ */

int64_t nitty_render_get_atlas_hits(void)   { return g_perf_atlas_hits; }
int64_t nitty_render_get_atlas_misses(void) { return g_perf_atlas_misses; }
int64_t nitty_render_get_dirty_cells(void)  { return g_perf_dirty_cells; }
int64_t nitty_render_get_total_cells(void)  { return g_perf_total_cells; }
void    nitty_render_reset_perf_stats(void)
{
    g_perf_atlas_hits   = 0;
    g_perf_atlas_misses = 0;
    g_perf_dirty_cells  = 0;
    g_perf_total_cells  = 0;
}

/* ── Forward declarations ──────────────────────────────────────────────── */
static void draw_cursor(cairo_t *cr); /* defined after nitty_render_frame */

/* ── Helper: draw a horizontal decoration line ────────────────────────── */

static void draw_hline(cairo_t *cr, double x, double y, double width, double thickness)
{
    cairo_set_line_width(cr, thickness);
    cairo_move_to(cr, x, y);
    cairo_line_to(cr, x + width, y);
    cairo_stroke(cr);
}

/* ── Helper: draw underline (all styles) ─────────────────────────────── */

static void draw_underline(cairo_t *cr, double x, double y_cell,
                            double width, int cell_height, int ul_style,
                            double r, double g, double b)
{
    cairo_set_source_rgb(cr, r, g, b);

    /* Underline Y: 2px above cell bottom */
    double ul_y = y_cell + (double)cell_height - 2.0;

    /* Reset dash before each underline draw */
    cairo_set_dash(cr, NULL, 0, 0.0);

    switch (ul_style) {
    case UL_SINGLE:
        draw_hline(cr, x, ul_y, width, 1.0);
        break;

    case UL_DOUBLE: {
        draw_hline(cr, x, ul_y - 2.0, width, 1.0);
        draw_hline(cr, x, ul_y,       width, 1.0);
        break;
    }

    case UL_CURLY: {
        /* Sine-wave approximation using cubic Bezier segments.
         * One full period = cell_width pixels, amplitude = 1.5px. */
        double amp   = 1.5;
        double phase = x;  /* start x */
        double period = (double)(cell_height > 8 ? cell_height : 8);
        double end_x  = x + width;
        double cx     = phase;
        double wave_y = ul_y;

        cairo_set_line_width(cr, 1.0);
        cairo_move_to(cr, cx, wave_y);

        while (cx < end_x) {
            double seg = period / 2.0;
            double ctrl1_x = cx + seg / 2.0;
            double ctrl2_x = cx + seg - seg / 2.0;
            double next_x  = cx + seg;
            if (next_x > end_x) next_x = end_x;
            /* Alternate up/down each half-period */
            double sign = (((int)((cx - phase) / seg)) % 2 == 0) ? 1.0 : -1.0;
            cairo_curve_to(cr,
                ctrl1_x, wave_y + sign * amp,
                ctrl2_x, wave_y + sign * amp,
                next_x,  wave_y);
            cx = next_x;
        }
        cairo_stroke(cr);
        break;
    }

    case UL_DOTTED: {
        double dash[2] = {2.0, 2.0};
        cairo_set_dash(cr, dash, 2, 0.0);
        draw_hline(cr, x, ul_y, width, 1.0);
        cairo_set_dash(cr, NULL, 0, 0.0);
        break;
    }

    case UL_DASHED: {
        double dash[2] = {4.0, 2.0};
        cairo_set_dash(cr, dash, 2, 0.0);
        draw_hline(cr, x, ul_y, width, 1.0);
        cairo_set_dash(cr, NULL, 0, 0.0);
        break;
    }

    default:
        /* Unknown style — fall back to single */
        draw_hline(cr, x, ul_y, width, 1.0);
        break;
    }
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

void nitty_render_set_cell_cp(int64_t col, int64_t row, int64_t codepoint)
{
    /* Encode Unicode codepoint to UTF-8 */
    char buf[8] = {0};
    uint32_t cp = (uint32_t)codepoint;

    if (cp < 0x80) {
        buf[0] = (char)cp;
        buf[1] = '\0';
    } else if (cp < 0x800) {
        buf[0] = (char)(0xC0 | (cp >> 6));
        buf[1] = (char)(0x80 | (cp & 0x3F));
        buf[2] = '\0';
    } else if (cp < 0x10000) {
        buf[0] = (char)(0xE0 | (cp >> 12));
        buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (cp & 0x3F));
        buf[3] = '\0';
    } else if (cp <= 0x10FFFF) {
        buf[0] = (char)(0xF0 | (cp >> 18));
        buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[3] = (char)(0x80 | (cp & 0x3F));
        buf[4] = '\0';
    } else {
        /* Invalid codepoint — use replacement character U+FFFD */
        buf[0] = (char)0xEF; buf[1] = (char)0xBF; buf[2] = (char)0xBD;
        buf[3] = '\0';
    }

    nitty_render_set_cell(col, row, buf);
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

/* v0.3.2: Set per-cell attribute flags */
void nitty_render_set_cell_flags(int64_t col, int64_t row, int64_t flags)
{
    if (col < 0 || col >= NITTY_MAX_COLS || row < 0 || row >= NITTY_MAX_ROWS) return;
    g_grid[(int)row][(int)col].flags = (int)flags;
    /* Expand grid bounds if needed (flags can be set on space/NUL cells) */
    if ((int)col + 1 > g_grid_cols) g_grid_cols = (int)col + 1;
    if ((int)row + 1 > g_grid_rows) g_grid_rows = (int)row + 1;
}

/* v0.3.2: Blink visibility toggle (called by GLib timer in shim) */
void nitty_render_set_blink_visible(int64_t visible)
{
    g_blink_visible = (visible != 0) ? 1 : 0;
}

/* v0.3.2: Query whether any cell currently has a blink flag */
int64_t nitty_render_has_blink_cells(void)
{
    return (int64_t)g_has_blink_cells;
}

/* v0.3.3: Cursor API */

void nitty_render_set_cursor(int64_t col, int64_t row,
                              int64_t shape, int64_t visible,
                              int64_t focused)
{
    g_cursor_col     = (int)col;
    g_cursor_row     = (int)row;
    g_cursor_shape   = (int)shape;
    g_cursor_visible = (visible != 0) ? 1 : 0;
    g_cursor_focused = (focused != 0) ? 1 : 0;
}

int64_t nitty_render_get_cursor_blink_phase(void)
{
    return (int64_t)g_cursor_blink_phase;
}

void nitty_render_set_cursor_blink_phase(int64_t phase)
{
    g_cursor_blink_phase = (phase != 0) ? 1 : 0;
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

int64_t nitty_render_get_cell_width(void)  { return (int64_t)g_cell_width; }
int64_t nitty_render_get_cell_height(void) { return (int64_t)g_cell_height; }
int64_t nitty_render_get_font_baseline(void) { return (int64_t)g_font_baseline; }

void nitty_render_clear_grid(void)
{
    memset(g_grid, 0, sizeof(g_grid));
    g_grid_cols = 0;
    g_grid_rows = 0;
    /* v0.3.6: don't force full_redraw here — let damage_compute() diff
       the cleared grid against the previous frame's snapshot.
       The cells are now all-zero which differs from prev if any were set. */
}

/* ── Render the grid ──────────────────────────────────────────────────── */

void nitty_render_frame(void *cr_ptr, int width, int height)
{
    cairo_t *cr = (cairo_t *)cr_ptr;
    if (cr == NULL) return;

    /* Measure font if needed — also invalidates atlas on font change */
    int font_was_changed = g_font_changed;
    measure_font(cr);
    if (g_cell_width <= 0 || g_cell_height <= 0) return;

    if (font_was_changed) {
        glyph_atlas_clear();
        g_full_redraw = 1;
    }

    /* Initialize atlas surface if needed */
    if (g_atlas_surface == NULL) {
        glyph_atlas_init(cr);
    }

    /* Reset blink-cell tracker for this frame */
    g_has_blink_cells = 0;

    /* Calculate visible grid dimensions */
    int visible_cols = width / g_cell_width;
    int visible_rows = height / g_cell_height;
    if (visible_cols > g_grid_cols) visible_cols = g_grid_cols;
    if (visible_rows > g_grid_rows) visible_rows = g_grid_rows;

    /* v0.3.6: Compute damage (diff current grid against previous snapshot) */
    damage_compute();

    int do_full = g_full_redraw;

    /* 1. Fill entire background (only on full redraw) */
    if (do_full) {
        cairo_set_source_rgb(cr,
            (double)g_bg_r / 1000.0,
            (double)g_bg_g / 1000.0,
            (double)g_bg_b / 1000.0
        );
        cairo_paint(cr);
    }

    if (visible_cols <= 0 || visible_rows <= 0) {
        damage_snapshot();
        return;
    }

    /* 2. Draw cell backgrounds — v0.3.6: batched + damage-aware */
    for (int r = 0; r < visible_rows; r++) {
        /* For partial redraw: clear dirty cells with default bg first */
        if (!do_full) {
            for (int c = 0; c < visible_cols; c++) {
                if (!g_dirty[r][c]) continue;
                /* Clear this cell's area with default background */
                cairo_set_source_rgb(cr,
                    (double)g_bg_r / 1000.0,
                    (double)g_bg_g / 1000.0,
                    (double)g_bg_b / 1000.0
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

        /* Background batching: merge adjacent cells with same bg color */
        int run_start = -1;
        int run_bg_r = 0, run_bg_g = 0, run_bg_b = 0;

        for (int c = 0; c <= visible_cols; c++) {
            int extend_run = 0;

            if (c < visible_cols) {
                GridCell *cell = &g_grid[r][c];
                /* Only process dirty cells (or all cells on full redraw) */
                if ((do_full || g_dirty[r][c]) && cell->has_bg) {
                    if (run_start >= 0 &&
                        cell->bg_r == run_bg_r &&
                        cell->bg_g == run_bg_g &&
                        cell->bg_b == run_bg_b) {
                        extend_run = 1;
                    } else {
                        /* Flush previous run if any */
                        if (run_start >= 0) {
                            cairo_set_source_rgb(cr,
                                (double)run_bg_r / 1000.0,
                                (double)run_bg_g / 1000.0,
                                (double)run_bg_b / 1000.0);
                            cairo_rectangle(cr,
                                (double)(run_start * g_cell_width),
                                (double)(r * g_cell_height),
                                (double)((c - run_start) * g_cell_width),
                                (double)g_cell_height);
                            cairo_fill(cr);
                        }
                        /* Start new run */
                        run_start = c;
                        run_bg_r = cell->bg_r;
                        run_bg_g = cell->bg_g;
                        run_bg_b = cell->bg_b;
                        extend_run = 1;
                    }
                }
            }

            if (!extend_run) {
                /* Flush run */
                if (run_start >= 0) {
                    cairo_set_source_rgb(cr,
                        (double)run_bg_r / 1000.0,
                        (double)run_bg_g / 1000.0,
                        (double)run_bg_b / 1000.0);
                    cairo_rectangle(cr,
                        (double)(run_start * g_cell_width),
                        (double)(r * g_cell_height),
                        (double)((c - run_start) * g_cell_width),
                        (double)g_cell_height);
                    cairo_fill(cr);
                    run_start = -1;
                }
            }
        }
    }

    /* 3. Draw text characters — v0.3.6: atlas-based + damage-aware */
    for (int r = 0; r < visible_rows; r++) {
        for (int c = 0; c < visible_cols; c++) {
            /* Skip clean cells on partial redraw */
            if (!do_full && !g_dirty[r][c]) continue;

            GridCell *cell = &g_grid[r][c];
            int flags = cell->flags;

            /* Track blink cells */
            if (flags & (FLAG_BLINK | FLAG_RAPID_BLINK)) {
                g_has_blink_cells = 1;
            }

            /* Skip continuation cells — drawn as part of WIDE cell */
            if (flags & FLAG_CONT) continue;

            /* Skip empty cells */
            if (cell->ch[0] == '\0' || cell->ch[0] == ' ') continue;

            /* Suppress blinking text when blink phase is hidden */
            if ((flags & (FLAG_BLINK | FLAG_RAPID_BLINK)) && !g_blink_visible) continue;

            /* v0.3.6: Atlas-based rendering */
            GlyphKey key = make_glyph_key(cell);
            GlyphEntry *entry = glyph_atlas_get(key, cell, cr);
            if (entry != NULL) {
                double px = (double)(c * g_cell_width);
                double py = (double)(r * g_cell_height);
                glyph_atlas_blit(entry, cr, px, py);
            }
        }
    }

    /* 4. Draw text decorations (underline, strikethrough, overline) */
    for (int r = 0; r < visible_rows; r++) {
        for (int c = 0; c < visible_cols; c++) {
            /* Skip clean cells on partial redraw */
            if (!do_full && !g_dirty[r][c]) continue;

            GridCell *cell = &g_grid[r][c];
            int flags = cell->flags;

            if (!(flags & (FLAG_UNDERLINE | FLAG_STRIKE | FLAG_OVERLINE))) continue;
            if (flags & FLAG_CONT) continue;

            double px = (double)(c * g_cell_width);
            double py = (double)(r * g_cell_height);
            int cell_span = (flags & FLAG_WIDE) ? 2 : 1;
            double cw = (double)(cell_span * g_cell_width);
            double ch = (double)g_cell_height;

            /* Resolve foreground color for decorations */
            double fr, fg, fb;
            if (cell->has_fg) {
                fr = (double)cell->fg_r / 1000.0;
                fg = (double)cell->fg_g / 1000.0;
                fb = (double)cell->fg_b / 1000.0;
            } else {
                fr = (double)g_fg_r / 1000.0;
                fg = (double)g_fg_g / 1000.0;
                fb = (double)g_fg_b / 1000.0;
            }

            if (flags & FLAG_UNDERLINE) {
                int ul_style = (flags & FLAG_UL_STYLE_MASK) >> FLAG_UL_STYLE_SHIFT;
                draw_underline(cr, px, py, cw, g_cell_height, ul_style, fr, fg, fb);
            }
            if (flags & FLAG_STRIKE) {
                cairo_set_source_rgb(cr, fr, fg, fb);
                draw_hline(cr, px, py + ch / 2.0, cw, 1.0);
            }
            if (flags & FLAG_OVERLINE) {
                cairo_set_source_rgb(cr, fr, fg, fb);
                draw_hline(cr, px, py + 1.0, cw, 1.0);
            }
        }
    }

    /* Reset dash in case it was left set */
    cairo_set_dash(cr, NULL, 0, 0.0);

    /* 5. Draw cursor on top of all cell content */
    draw_cursor(cr);

    /* 6. Draw scrollbar overlay (v0.3.4) */
    draw_scrollbar(cr, width, height);

    /* v0.3.6: Snapshot grid for next frame's damage computation */
    damage_snapshot();
}

/* v0.3.3: Cursor drawing — called at end of nitty_render_frame() */
static void draw_cursor(cairo_t *cr)
{
    if (!g_cursor_visible) return;
    if (g_cell_width <= 0 || g_cell_height <= 0) return;
    if (g_cursor_col < 0 || g_cursor_row < 0) return;

    double x  = (double)(g_cursor_col * g_cell_width);
    double y  = (double)(g_cursor_row * g_cell_height);
    /* v0.3.5: block cursor spans 2 cells when on a wide char */
    int is_wide = 0;
    if (g_cursor_row < NITTY_MAX_ROWS && g_cursor_col < NITTY_MAX_COLS) {
        is_wide = (g_grid[g_cursor_row][g_cursor_col].flags & FLAG_WIDE) ? 1 : 0;
    }
    double cw = is_wide ? (double)(2 * g_cell_width) : (double)g_cell_width;
    double ch = (double)g_cell_height;

    /* Cursor color: use default foreground */
    double cur_r = (double)g_fg_r / 1000.0;
    double cur_g = (double)g_fg_g / 1000.0;
    double cur_b = (double)g_fg_b / 1000.0;

    if (g_cursor_shape == 0) {
        /* ── Block cursor ── */
        if (g_cursor_focused) {
            /* Filled block */
            cairo_set_source_rgb(cr, cur_r, cur_g, cur_b);
            cairo_rectangle(cr, x, y, cw, ch);
            cairo_fill(cr);
            /* Redraw character with inverted color (bg color on cursor) */
            if (g_cursor_row < NITTY_MAX_ROWS && g_cursor_col < NITTY_MAX_COLS) {
                GridCell *cell = &g_grid[g_cursor_row][g_cursor_col];
                if (cell->ch[0] != '\0' && cell->ch[0] != ' ') {
                    PangoLayout *lo = pango_cairo_create_layout(cr);
                    pango_layout_set_font_description(lo, select_font(cell->flags));
                    if (is_wide) {
                        pango_layout_set_width(lo, 2 * g_cell_width * PANGO_SCALE);
                        pango_layout_set_alignment(lo, PANGO_ALIGN_CENTER);
                    }
                    pango_layout_set_text(lo, cell->ch, -1);
                    /* Draw in background color */
                    cairo_set_source_rgb(cr,
                        (double)g_bg_r / 1000.0,
                        (double)g_bg_g / 1000.0,
                        (double)g_bg_b / 1000.0);
                    cairo_move_to(cr, x, y);
                    pango_cairo_show_layout(cr, lo);
                    g_object_unref(lo);
                }
            }
        } else {
            /* Hollow block outline */
            cairo_set_source_rgb(cr, cur_r, cur_g, cur_b);
            cairo_set_line_width(cr, 1.0);
            cairo_rectangle(cr, x + 0.5, y + 0.5, cw - 1.0, ch - 1.0);
            cairo_stroke(cr);
        }
    } else if (g_cursor_shape == 1) {
        /* ── Underline cursor: 2px bar at cell bottom ── */
        cairo_set_source_rgb(cr, cur_r, cur_g, cur_b);
        if (g_cursor_focused) {
            cairo_rectangle(cr, x, y + ch - 2.0, cw, 2.0);
            cairo_fill(cr);
        } else {
            cairo_set_line_width(cr, 1.0);
            cairo_rectangle(cr, x + 0.5, y + ch - 2.5, cw - 1.0, 1.0);
            cairo_stroke(cr);
        }
    } else {
        /* ── Bar (I-beam) cursor: 2px bar at cell left ── */
        cairo_set_source_rgb(cr, cur_r, cur_g, cur_b);
        if (g_cursor_focused) {
            cairo_rectangle(cr, x, y, 2.0, ch);
            cairo_fill(cr);
        } else {
            cairo_set_line_width(cr, 1.0);
            cairo_rectangle(cr, x + 0.5, y + 0.5, 1.0, ch - 1.0);
            cairo_stroke(cr);
        }
    }
}

/* ── v0.3.4: Scroll info ──────────────────────────────────────────────── */

void nitty_render_set_scroll_info(int64_t offset, int64_t total, int64_t visible)
{
    g_scroll_offset  = offset;
    g_scroll_total   = total;
    g_scroll_visible = visible;
}

/* ── v0.3.5: Font system setters ─────────────────────────────────────── */

void nitty_render_set_line_height(int64_t lh_x1000)
{
    int v = (int)lh_x1000;
    if (v < 800)  v = 800;   /* clamp: minimum 0.8× */
    if (v > 3000) v = 3000;  /* clamp: maximum 3.0× */
    if (v != g_line_height_x1000) {
        g_line_height_x1000 = v;
        g_font_changed = 1;
    }
}

void nitty_render_set_letter_spacing(int64_t px)
{
    int v = (int)px;
    if (v < 0)   v = 0;    /* no negative spacing */
    if (v > 100) v = 100;  /* sanity cap */
    if (v != g_letter_spacing) {
        g_letter_spacing = v;
        g_font_changed = 1;
    }
}

void nitty_render_set_ligatures(int64_t enabled)
{
    int v = (enabled != 0) ? 1 : 0;
    if (v == g_ligatures_enabled) return;
    g_ligatures_enabled = v;

    /* Free the cached attr list — will be recreated if still disabled */
    if (g_no_ligature_attrs != NULL) {
        pango_attr_list_unref(g_no_ligature_attrs);
        g_no_ligature_attrs = NULL;
    }

    if (!g_ligatures_enabled) {
        /* Build attr list: disable common ligature features */
        g_no_ligature_attrs = pango_attr_list_new();
        PangoAttribute *attr = pango_attr_font_features_new("liga=0, calt=0, dlig=0");
        pango_attr_list_insert(g_no_ligature_attrs, attr);
    }
}

/* ── v0.3.4: Scrollbar overlay ────────────────────────────────────────── */

static void draw_scrollbar(cairo_t *cr, int width, int height)
{
    /* Only show when user has scrolled up */
    if (g_scroll_offset <= 0) return;
    if (g_scroll_total <= g_scroll_visible) return;
    if (height <= 0) return;

    /* ── Track: right 8px, full height, semi-transparent dark ── */
    cairo_set_source_rgba(cr, 0.15, 0.15, 0.15, 0.45);
    cairo_rectangle(cr, (double)(width - 8), 0.0, 8.0, (double)height);
    cairo_fill(cr);

    /* ── Handle: proportional position within track ── */
    double frac_vis = (double)g_scroll_visible / (double)g_scroll_total;
    double frac_pos = 1.0 - (double)g_scroll_offset /
                             (double)(g_scroll_total - g_scroll_visible);

    /* Clamp to [0..1] */
    if (frac_vis < 0.02) frac_vis = 0.02;
    if (frac_vis > 1.0)  frac_vis = 1.0;
    if (frac_pos < 0.0)  frac_pos = 0.0;
    if (frac_pos > 1.0)  frac_pos = 1.0;

    double handle_h = frac_vis * (double)height;
    if (handle_h < 8.0) handle_h = 8.0;

    double handle_y = frac_pos * ((double)height - handle_h);

    cairo_set_source_rgba(cr, 0.75, 0.75, 0.75, 0.70);
    cairo_rectangle(cr, (double)(width - 7), handle_y + 1.0, 6.0, handle_h - 2.0);
    cairo_fill(cr);
}
