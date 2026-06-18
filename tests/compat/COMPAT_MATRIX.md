# Nitty v0.11.1 Compatibility Matrix

Tracks which programs and features are verified to work correctly in Nitty's terminal
pipeline. Tests are VT-sequence replay (headless) unless marked **[live]** (manual smoke test).

**Legend**: ✅ pass · ⚠️ partial · ❌ fail · 🔵 untested

---

## Shells

| Program | Feature | Status | Test |
|---------|---------|--------|------|
| bash 5.2 | ANSI color PS1 (bold green user@host) | ✅ | [test_shell_compat T01–T05](test_shell_compat.npk) |
| bash 5.2 | CR+LF cursor movement after command | ✅ | [test_shell_compat T06–T10](test_shell_compat.npk) |
| bash 5.2 | Tab completion (cursor hide/overwrite/show) | ✅ | [test_shell_compat T11–T15](test_shell_compat.npk) |
| bash 5.2 | History arrow-up (ESC[1A + erase-line) | ✅ | [test_shell_compat T16–T20](test_shell_compat.npk) |
| bash 5.2 | OSC 0 window title set | ✅ | [test_shell_compat T21–T25](test_shell_compat.npk) |
| bash/zsh/fish | Bracketed paste mode (DECSET 2004) | ✅ | [test_shell_compat T26–T30](test_shell_compat.npk) |
| bash/zsh/fish | SGR 256-color fg/bg | ✅ | [test_shell_compat T31–T35](test_shell_compat.npk) |
| bash/zsh/fish | SGR truecolor (38;2;r;g;b) | ✅ | [test_shell_compat T36–T40](test_shell_compat.npk) |
| zsh 5.9 | Live prompt render with starship | ✅ **[live]** | Manual smoke test |
| fish 3.7 | Live prompt render with starship | ✅ **[live]** | Manual smoke test |

---

## TUI Applications

| Program | Feature | Status | Test |
|---------|---------|--------|------|
| vim | Alternate screen entry (?1049h) | ✅ | [test_tui_compat T01–T05](test_tui_compat.npk) |
| vim | Cursor hide/show during redraw | ✅ | [test_tui_compat T06–T10](test_tui_compat.npk) |
| vim | Absolute cursor positioning (ESC[r;cH) | ✅ | [test_tui_compat T11–T15](test_tui_compat.npk) |
| vim | Syntax highlighting (bold + SGR color) | ✅ | [test_tui_compat T16–T20](test_tui_compat.npk) |
| vim | Alternate screen exit (?1049l) | ✅ | [test_tui_compat T21–T25](test_tui_compat.npk) |
| htop | Block-character meters (▓░█ U+2591–2593) | ✅ | [test_tui_compat T26–T30](test_tui_compat.npk) |
| htop | Reverse-video function-key bar | ✅ | [test_tui_compat T31–T35](test_tui_compat.npk) |
| tmux | Status bar (green text on dark bg) | ✅ | [test_tui_compat T36–T40](test_tui_compat.npk) |
| tmux/mc | Box-drawing border chars (─│┌┐└┘═) | ✅ | [test_tui_compat T41–T45](test_tui_compat.npk) |
| ranger | Multi-column absolute positioning | ✅ | [test_tui_compat T46–T50](test_tui_compat.npk) |

---

## Developer Tools

| Program | Feature | Status | Test |
|---------|---------|--------|------|
| git log | Colored graph (yellow *, red \|, cyan branch) | ✅ | [test_dev_tools_compat T01–T05](test_dev_tools_compat.npk) |
| git diff | Red deletions, green additions | ✅ | [test_dev_tools_compat T06–T10](test_dev_tools_compat.npk) |
| git status | Bold-green modified, red untracked | ✅ | [test_dev_tools_compat T11–T15](test_dev_tools_compat.npk) |
| gcc/clang | Color diagnostics (bold error, green caret) | ✅ | [test_dev_tools_compat T16–T20](test_dev_tools_compat.npk) |
| python3 REPL | >>> and ... prompts, ANSI passthrough | ✅ | [test_dev_tools_compat T21–T25](test_dev_tools_compat.npk) |
| less | Alt screen, status bar, search highlight | ✅ | [test_dev_tools_compat T26–T30](test_dev_tools_compat.npk) |
| man | Bold headers (SGR 1), underlined options (SGR 4) | ✅ | [test_dev_tools_compat T31–T35](test_dev_tools_compat.npk) |
| curl | CR-overwrite progress bar | ✅ | [test_dev_tools_compat T36–T40](test_dev_tools_compat.npk) |
| top | ESC[2J full-screen refresh, column header reverse | ✅ | [test_dev_tools_compat T41–T45](test_dev_tools_compat.npk) |
| general | Multi-param SGR, SGR m (reset), strikethrough | ✅ | [test_dev_tools_compat T46–T50](test_dev_tools_compat.npk) |

---

## Fonts & Glyphs

| Feature | Status | Test |
|---------|--------|------|
| Box-drawing: ─│┌┐└┘ (U+2500–2518) | ✅ | [test_fonts_glyphs_compat T01–T06](test_fonts_glyphs_compat.npk) |
| Block elements: █▓▒░▀▄ (U+2580–2593) | ✅ | [test_fonts_glyphs_compat T07–T12](test_fonts_glyphs_compat.npk) |
| Powerline U+E0C7 rounded-left sep (starship) | ✅ | [test_fonts_glyphs_compat T13](test_fonts_glyphs_compat.npk) |
| Powerline U+E0B4 rounded-right sep (starship) | ✅ | [test_fonts_glyphs_compat T14](test_fonts_glyphs_compat.npk) |
| Powerline U+E0C6 flame sep (starship) | ✅ | [test_fonts_glyphs_compat T15](test_fonts_glyphs_compat.npk) |
| Nerd Font U+F30E git branch icon (starship) | ✅ | [test_fonts_glyphs_compat T16](test_fonts_glyphs_compat.npk) |
| Nerd Font U+E0B6 soft sep (starship) | ✅ | [test_fonts_glyphs_compat T17](test_fonts_glyphs_compat.npk) |
| Nerd Font U+E235 python icon (starship) | ✅ | [test_fonts_glyphs_compat T18](test_fonts_glyphs_compat.npk) |
| Starship truecolor + Nerd Font full segment | ✅ | [test_fonts_glyphs_compat T19–T22](test_fonts_glyphs_compat.npk) |
| Emoji: ★ (U+2605), ● (U+25CF) | ✅ | [test_fonts_glyphs_compat T23–T24](test_fonts_glyphs_compat.npk) |
| Emoji: 🔑 U+1F511 (4-byte, from starship output) | ✅ | [test_fonts_glyphs_compat T25](test_fonts_glyphs_compat.npk) |
| Combining chars: e+U+0301→é, a+U+0300→à | ✅ | [test_fonts_glyphs_compat T26–T30](test_fonts_glyphs_compat.npk) |
| CJK wide: 中 (U+4E2D), 日 (U+65E5), ア (U+30A2) | ✅ | [test_fonts_glyphs_compat T31–T35](test_fonts_glyphs_compat.npk) |
| Control rejection: NUL, BEL, BS, DEL | ✅ | [test_fonts_glyphs_compat T36–T40](test_fonts_glyphs_compat.npk) |

---

## Known Limitations

| Item | Notes |
|------|-------|
| Sixel / Kitty image protocol | Not implemented. ranger image preview, kitty graphics not supported in v0.11.x. |
| zsh right-prompt (RPROMPT) | VT sequence tested; live rendering depends on terminal width reporting (future: PTY resize). |
| tmux nested sessions | Nested Nitty-in-tmux not tested in headless suite; manual verification only. |
| CJK wide char double-cell blank | Parser advances cursor by 2; the "phantom" second cell not explicitly blanked in all renderers. |

---

## Environment

- **Test date**: 2026-06-18
- **Branch**: `dev-0.11.x`
- **starship**: v1.24.2 (Nerd Font / Powerline theme — rounded separators, flame separators)
- **zsh**: 5.9 · **fish**: 3.7.0 · **bash**: 5.2.21
- **Test framework**: `tests/e2e/framework.npk` (v0.11.0 headless pipeline)
