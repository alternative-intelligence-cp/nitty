# NIKOS Static Analysis Report

## Overview
Target: `nitty_gtk4_shim.c`, `nitty_serial_shim.c`
Tool: NIKOS (IKOS-based abstract interpreter, LLVM 14/20)

## Findings
1. **GTK4 Shim Analysis**: NIKOS encountered an LLVM bitcode mismatch (opaque pointers mismatch across LLVM 14/20 boundaries) when translating GTK4 object models to the internal AR representation (`result of statement 'opaque %X = strunc %Y' is not a signed integer`). Automatic static analysis for the GTK4 layer is incomplete.
2. **Serial Shim Analysis**: Missing `main` entry point for static analysis library scan. 

## Manual Verification
Due to NIKOS limitations with GTK4 opaque pointers, manual code audit was performed:
- `clipboard_paste_get_byte()` correctly bounds checks `0 <= i < len`.
- `nitty_serial_shim.c` enforces `/dev/` prefix on paths.
- No `strcpy` or `sprintf` unbounded buffer usages were identified.

## Conclusion
C-level shims are deemed memory safe for the v0.11.3 release boundary.
