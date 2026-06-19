# Nitty CI/CD Pipeline

Nitty uses GitHub Actions to automate building, testing, benchmarking, and releasing packages.

## Workflows

### 1. CI Build and Test (`ci.yml`)
- **Triggers:** Pushes and Pull Requests against `main` and `dev-*` branches.
- **Purpose:** Ensures the codebase compiles and passes standard tests on supported platforms (Ubuntu 24.04 and 22.04).
- **Checks Performed:** 
  - License header validation
  - Nitpick compiler linting and NIKOS safety analysis
  - `npkbld test` (Unit tests)
  - `tests/e2e/run_e2e.sh` (End-to-End tests)

### 2. PR Quality Checks (`pr-checks.yml`)
- **Triggers:** Opening, editing, or synchronizing Pull Requests.
- **Purpose:** Enforces metadata standards before code can be merged.
- **Checks Performed:** Validates PR description length and ensures commit messages follow conventions (`vX.Y.Z: ...` or semantic commit formats).

### 3. Performance Benchmarks (`bench.yml`)
- **Triggers:** Pushes to `main`, or manual `workflow_dispatch`.
- **Purpose:** Tracks throughput and latency metrics to prevent performance regressions.
- **Behavior:** On PRs, if it detects >10% degradation compared to `main`, it will fail the check and leave a comment.

### 4. Weekly Compatibility Test (`compat.yml`)
- **Triggers:** Every Sunday at 02:00 UTC.
- **Purpose:** Runs long-running stress tests, strict VT-parser compatibility checks, and security audits that are too heavy to run on every commit.

### 5. Automated Release (`release.yml`)
- **Triggers:** Pushing a new git tag matching `v*.*.*`.
- **Purpose:** Compiles the application and generates Linux distribution packages (`.deb`, `.rpm`, `.AppImage`, `.flatpak`), then publishes them to GitHub Releases.
- **How to Release:**
  1. Complete the pre-release checklist.
  2. Run `git tag -a v0.14.X -m "Release v0.14.X"`
  3. Run `git push origin v0.14.X`

## Required Secrets
The workflows currently rely only on the automatically generated `GITHUB_TOKEN` to create releases and leave comments. No external secrets are required for basic operation.

## Troubleshooting

- **"Missing license headers" failure:** Ensure any new `.npk` or `.c` files include the AGPL-3.0 header.
- **Benchmark failure:** The benchmark suite depends on high-resolution timers. In virtualized CI environments, CPU starvation can sometimes trigger false positives. Retry the job to rule out variance.
- **AppImage/Flatpak build failures:** Ensure `linuxdeploy` and `flatpak-builder` URLs/manifests are accessible and valid.
