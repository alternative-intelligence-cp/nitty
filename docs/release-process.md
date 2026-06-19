# Release Process

This document outlines the standard operating procedure for releasing a new version of Nitty.

## Versioning

Nitty follows Semantic Versioning (`MAJOR.MINOR.PATCH`).
- While in `0.X.Y`, minor versions represent significant feature drops, and patch versions represent bug fixes or minor enhancements.
- A `1.0.0` release will signal strict API stability, particularly for the Plugin API.

## Release Workflow

All development targeting a specific minor release happens on a dedicated branch (e.g., `dev-0.13.x`). Once all roadmap milestones for that version are complete, the following checklist must be executed.

### Pre-Release Checklist

1. **Testing:**
   - Run `npkbld test`. All unit and integration tests must pass.
   - Run the E2E compatibility test suite (`tests/terminal/test_compat.npk`).
   - Run the stress tests and security audit suites.
2. **Benchmarking:**
   - Run the benchmarking suite (`tests/bench/`). Ensure throughput and latency have not regressed compared to the previous stable release.
3. **Documentation:**
   - Ensure the User Manual (`docs/user-manual/`) reflects any new features or changed config settings.
   - Ensure `CHANGELOG.md` is updated with the new version and a summary of changes.
4. **Version Bumps:**
   - Update `app_version()` in `src/core/app.npk`.
   - Update the startup log message in `app.npk`.
   - Update `version` string in `build.abc` if applicable.

### Tagging and Building

1. Commit the final version bumps with message: `Release vX.Y.Z`
2. Merge the `dev-X.Y.x` branch into `main`.
3. Create an annotated git tag:
   ```bash
   git tag -a vX.Y.Z -m "Nitty vX.Y.Z"
   git push origin main --tags
   ```
4. Trigger the CI pipeline to generate packages (`.deb`, `.rpm`, `AppImage`).

### Hotfix Process (Patch Releases)

If a critical bug is found in the current stable release:
1. Check out the corresponding `dev-X.Y.x` branch.
2. Fix the bug, bump the patch version (e.g., `0.13.0` -> `0.13.1`), and update `CHANGELOG.md`.
3. Tag and release.
4. `cherry-pick` the bugfix commit onto the current working development branch to ensure the regression doesn't reappear in the next minor release.
