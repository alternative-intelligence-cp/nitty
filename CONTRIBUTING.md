# Contributing to Nitty

Thank you for your interest in contributing to Nitty! This document outlines the process for setting up your development environment, our coding standards, and how to submit your work.

## Development Environment Setup

Nitty is written in the Nitpick programming language and heavily relies on C shims for system integration (GTK4, LibSSH2, termios).

### Prerequisites
- **Nitpick Compiler (`nitpickc`)** and build system (`npkbld`)
- **LLVM 20** (required backend for Nitpick)
- **GTK4 Development Headers** (`libgtk-4-dev` or `gtk4-devel`)
- **LibSSH2 Development Headers** (`libssh2-1-dev`)
- **OpenSSL Development Headers** (`libssl-dev`)
- **C Compiler** (`clang` or `gcc`)

### Building from Source
```bash
git clone https://github.com/altintel/nitty.git
cd nitty

# Build the project (resolves dependencies via build.abc)
npkbld build

# Run the compiled binary
./.nitpick_make/build/nitty
```

### Running Tests
All new features must include tests. To run the test suite:
```bash
# Run all unit and integration tests
npkbld test

# Run the specific performance benchmarking suite
npkbld test --target bench
```

## Code Style Guide

Nitpick is a strictly typed language. Please adhere to the following conventions:

1. **Naming Conventions:**
   - Functions and variables: `snake_case`
   - Structs and enums: `PascalCase`
   - Constants (and 'global' config limits): `UPPER_SNAKE_CASE`

2. **Error Handling:**
   - Use the `Result<T>` type for functions that can fail.
   - For C shims, follow standard UNIX conventions (return `0` on success, `-1` on error) and handle the error boundary explicitly in the `.npk` wrapper.

3. **Memory Management:**
   - Remember that Nitpick uses a garbage collector. Do **not** pass pointers to Nitpick-managed strings or arrays into C-callbacks that execute asynchronously, as the GC may move them. Always copy data to static C buffers or use malloc/free explicitly across the FFI boundary.

4. **Comments:**
   - Use `///` for documentation comments on public (`pub func`) API boundaries.
   - Use `//` for internal implementation details.

## Git Workflow

1. **Branch Naming:**
   - `feature/name-of-feature`
   - `fix/issue-description`
   - (Release branches like `dev-0.13.x` are managed by maintainers).

2. **Commit Messages:**
   - Format: `vX.Y.Z: descriptive message` or `feat(module): description`.
   - Keep the first line under 72 characters.
   - Explain *why* the change was made in the body if it is complex.

3. **Pull Requests:**
   - Ensure `npkbld test` passes locally.
   - If modifying the VT parser (`src/terminal/vt_parser.npk`), ensure you haven't broken the `vttest` conformance suite.
   - If making a performance-sensitive change (like the render loop), include output from the benchmark suite.

Thank you for helping make Nitty the best terminal emulator available!
