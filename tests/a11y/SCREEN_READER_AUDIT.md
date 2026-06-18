# Nitty v0.13.0 — Screen Reader Audit

**Standard**: WCAG 2.1 Level AA + AT-SPI 2 (Linux)  
**Screen readers under test**: Orca (primary), Speakup (console)  
**GTK version**: GTK 4.x (GtkAccessibleText implementation)  
**Last audit**: _(fill in date)_  
**Tester**: _(fill in)_

---

## Setup

```bash
# 1. Enable Orca before launching Nitty
orca &

# 2. Build and launch Nitty
npkbld build
./nitty

# 3. Confirm AT-SPI bus is running
ls /run/user/$(id -u)/at-spi-bus 2>/dev/null && echo "AT-SPI bus OK"

# 4. Check Nitty appears in Orca's object tree
orca --debug-to-file /tmp/orca-nitty.log &
```

---

## Test Cases

### TC-01: Window accessible name
- **Action**: Launch Nitty and switch to it via Alt+Tab.
- **Expected**: Orca announces "Nitty" (or "Nitty — bash") as the window name.
- **Result**: [ ] PASS  [ ] FAIL  [ ] N/A
- **Notes**: _______

### TC-02: Terminal widget accessible role
- **Action**: Focus the terminal drawing area.
- **Expected**: Orca announces role "terminal" (GtkAccessibleRole TERMINAL).
- **Result**: [ ] PASS  [ ] FAIL  [ ] N/A
- **Notes**: _______

### TC-03: Initial screen content read-back
- **Action**: Press Orca+T (flat review read current line).
- **Expected**: Orca reads the current prompt line (e.g. "user@host:~ $ ").
- **Result**: [ ] PASS  [ ] FAIL  [ ] N/A
- **Notes**: _______

### TC-04: New output announced (live region)
- **Setup**: `a11y.announce_output = 1` in config.
- **Action**: Run `echo hello` in the terminal.
- **Expected**: Orca announces "hello" within 500 ms of output appearing.
- **Result**: [ ] PASS  [ ] FAIL  [ ] N/A
- **Notes**: _______

### TC-05: Caret position tracking
- **Action**: Move through history with up/down arrows, then press Left.
- **Expected**: Orca reports updated caret column position.
- **Result**: [ ] PASS  [ ] FAIL  [ ] N/A
- **Notes**: _______

### TC-06: Tab switch announcement
- **Action**: Open two tabs (Ctrl+Shift+T) and switch with Ctrl+PgDn.
- **Expected**: Orca announces tab name/number on switch.
- **Result**: [ ] PASS  [ ] FAIL  [ ] N/A
- **Notes**: _______

### TC-07: Screen clear announcement
- **Action**: Run `clear` in the terminal.
- **Expected**: Orca announces "screen cleared" (or equivalent).
- **Result**: [ ] PASS  [ ] FAIL  [ ] N/A
- **Notes**: _______

### TC-08: Ctrl+F6 skip-navigation (WCAG 2.4.1)
- **Action**: Press Ctrl+F6 while focused on the terminal.
- **Expected**: Focus advances to the next landmark region (tab bar or sidebar).
- **Result**: [ ] PASS  [ ] FAIL  [ ] N/A
- **Notes**: _______

### TC-09: High-contrast theme — readability
- **Action**: Set theme to `high-contrast-dark` or `high-contrast-light` in config.
- **Expected**: Terminal renders with ≥ 7:1 contrast ratio (WCAG AAA). Orca does not announce theme change.
- **Result**: [ ] PASS  [ ] FAIL  [ ] N/A
- **Notes**: _______

### TC-10: High-contrast auto-detect (system preference)
- **Setup**: Enable "High Contrast" in GNOME Accessibility settings.
- **Action**: Launch Nitty.
- **Expected**: Nitty automatically applies `high-contrast-dark` theme without user config.
- **Result**: [ ] PASS  [ ] FAIL  [ ] N/A
- **Notes**: _______

### TC-11: Keyboard-only navigation (no pointer)
- **Action**: Navigate entire Nitty UI (tabs, sidebar, dialogs) using only Tab/Enter/Escape/Arrow keys.
- **Expected**: All interactive elements reachable; focus indicator visible at all times.
- **Result**: [ ] PASS  [ ] FAIL  [ ] N/A
- **Notes**: _______

### TC-12: Settings dialog accessibility
- **Action**: Open settings (Ctrl+Comma), navigate all controls with keyboard.
- **Expected**: All labels read correctly; sliders announce values; dropdowns announce selection.
- **Result**: [ ] PASS  [ ] FAIL  [ ] N/A
- **Notes**: _______

### TC-13: Connection Manager sidebar
- **Action**: Open connection manager, navigate profiles with Tab/Enter.
- **Expected**: Profile names announced; connect/disconnect buttons reachable.
- **Result**: [ ] PASS  [ ] FAIL  [ ] N/A
- **Notes**: _______

### TC-14: Bell / notification announcement
- **Setup**: `terminal.bell_notify = 1` in config.
- **Action**: Trigger a bell (e.g. `echo -e '\a'`).
- **Expected**: Orca announces "bell" or the notification toast text is read.
- **Result**: [ ] PASS  [ ] FAIL  [ ] N/A
- **Notes**: _______

### TC-15: Reduce motion — no blink
- **Setup**: `a11y.reduce_motion = 1` in config.
- **Action**: Observe terminal cursor.
- **Expected**: Cursor does not blink regardless of `cursor.blink` config.
- **Result**: [ ] PASS  [ ] FAIL  [ ] N/A
- **Notes**: _______

---

## Known Issues / Regressions

| # | Description | Severity | Filed | Fixed |
|---|-------------|----------|-------|-------|
| 1 | _(none yet)_ | — | — | — |

---

## AT-SPI Tree Verification

Run to dump the AT-SPI object tree for Nitty:

```bash
# Requires accerciser or python-atspi
python3 -c "
import gi
gi.require_version('Atspi', '2.0')
from gi.repository import Atspi
desktop = Atspi.get_desktop(0)
for app in desktop:
    if 'itty' in str(app.get_name()):
        print(f'App: {app.get_name()}')
        for child in app:
            print(f'  Child: {child.get_name()} role={child.get_role_name()}')
"
```

Expected output should include:
```
App: Nitty
  Child: Nitty Terminal  role=terminal
```

---

## Audit Sign-off

| Section | Auditor | Date | PASS/FAIL |
|---------|---------|------|-----------|
| TC-01 to TC-07 (AT-SPI core) | | | |
| TC-08 to TC-11 (Keyboard / HC) | | | |
| TC-12 to TC-15 (UI / Config) | | | |

**Overall WCAG 2.1 AA Conformance**: [ ] Conformant  [ ] Partially Conformant  [ ] Non-Conformant
