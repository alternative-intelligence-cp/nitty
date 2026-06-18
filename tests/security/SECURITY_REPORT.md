# Nitty v0.11.3 Security Report

## Executive Summary
This report summarizes the security hardening measures implemented in Nitty v0.11.3 to address critical vulnerabilities.

## Addressed Findings
- **CRITICAL [MITM]**: SSH Host Key verification is now correctly enforced with TOFU logic. Connections are hard-aborted on host key mismatches.
- **HIGH [Credentials]**: SSH passwords and master vault phrases are securely zeroed out (`explicit_bzero`) after usage.
- **MEDIUM [Plugin Sandboxing]**: Added strict regex-based plugin name allowlisting (`[a-zA-Z0-9_-]+`) and path traversal protections.
- **MEDIUM [Config Safety]**: `terminal.shell`, `terminal.link_handler`, and `plugins.search_dirs` now perform path normalization and sanity checks (rejecting shell injection characters like `|`, `;`, `&`, ``` ` ```).
- **LOW [Memory]**: Validated FFI shim bounds checks.

## Ongoing Work
- Deep plugin sandboxing and capability enforcement is stubbed in `api.npk` and scheduled for v0.12.x.
